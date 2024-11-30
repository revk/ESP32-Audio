/* Audio app */
/* Copyright ©2024 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static const char TAG[] = "Audio";

#include "revk.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/i2c.h>
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <driver/rtc_io.h>
#include <esp_http_server.h>
#include "fft.h"
#include "math.h"
#include "esp_vfs_fat.h"
#include <sys/dirent.h>
#include <sip.h>

typedef int16_t audio_t;
#define	audio_max	32767

struct
{
   char c;
   const char *m;
} morse[] = {
   {'A', ".-"},
   {'B', "-..."},
   {'C', "-.-."},
   {'D', "-.."},
   {'E', "."},
   {'F', "..-."},
   {'G', "--."},
   {'H', "...."},
   {'I', ".."},
   {'J', ".---"},
   {'K', "-.-"},
   {'L', ".-.."},
   {'M', "--"},
   {'N', "-."},
   {'O', "---"},
   {'P', ".--."},
   {'Q', "--.-"},
   {'R', ".-."},
   {'S', "..."},
   {'T', "-"},
   {'U', "..-"},
   {'V', "...-"},
   {'W', ".--"},
   {'X', "-..-"},
   {'Y', "-.--"},
   {'Z', "--.-"},
   {'1', ".----"},
   {'2', "..---"},
   {'3', "...--"},
   {'4', "....-"},
   {'5', "....."},
   {'6', "-...."},
   {'7', "--..."},
   {'8', "---.."},
   {'9', "----."},
   {'0', "-----"},
};

char *morsemessage = NULL;      // Malloc'd
char *tones = NULL;             // Malloc'd

typedef enum
{
   MIC_IDLE,
   MIC_SIP,
   MIC_RECORD,
} mic_mode_t;
mic_mode_t mic_mode = 0;

typedef enum
{
   SPK_IDLE,
   SPK_SIP,
   SPK_TONE,
   SPK_WAV,
} spk_mode_t;
spk_mode_t spk_mode = 0;

sip_state_t sip_mode;

struct
{
   uint8_t die:1;               // Shutting down
   uint8_t sdpresent:1;         // SD present
   uint8_t doformat:1;          // SD format
   uint8_t dodismount:1;        // Dismount SD
   uint8_t micon:1;             // Sounds required
   uint8_t sharedi2s:1;         // I2S shared for Mic and Spk
   uint8_t ha:1;                // Send HA config
   uint8_t usb:1;               // USB connected
} b = { 0 };

const char sd_mount[] = "/sd";
char rgbsd = 0;                 // Colour for SD card
const char *cardstatus = NULL;  // Status of SD card
uint64_t sdsize = 0,            // SD card data
   sdfree = 0;
FILE *volatile sdfile = NULL;

i2s_chan_handle_t mic_handle = { 0 };

#define	MICMS		100
#define	MICQUEUE	32
uint8_t micchannels = 0;        // Channels (1 or 2)
uint8_t micbytes = 0;           // Bytes per channel (2, 3, or 4)
uint32_t micsamples = 0;        // Samples per collection
uint32_t micfreq = 0;           // Actual sample rate
uint8_t *micaudio[MICQUEUE] = { 0 };

#define	SPKMS		100
i2s_chan_handle_t spk_handle = { 0 };

uint8_t spkchannels = 0;        // Channels (1 or 2)
uint8_t spkbytes = 0;           // Bytes per channel (2, 3, or 4)
uint32_t spksamples = 0;        // Samples per collection
uint32_t spkfreq = 0;           // Actual sample rate

volatile uint8_t sdin = 0,
   sdout = 0;

static httpd_handle_t webserver = NULL;

const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   if (client || !prefix || target || strcmp (prefix, topiccommand) || !suffix)
      return NULL;              // Not for us or not a command from main MQTTS
   if (!strcasecmp (suffix, "morse"))
   {
      if (morsemessage)
         return "Wait";
      if (jo_here (j) != JO_STRING)
         return "JSON string";
      // TODO allow object with custom WPM, and so on
      morsemessage = jo_strdup (j);
      return NULL;
   }
   if (!strcasecmp (suffix, "dtmf") || !strcasecmp (suffix, "tone"))
   {
      if (tones)
         return "Wait";
      if (jo_here (j) != JO_STRING)
         return "JSON string";
      // TODO allow object with custom timings, and so on
      tones = jo_strdup (j);
      return NULL;
   }
   if (!strcasecmp (suffix, "format"))
   {
      b.doformat = 1;
      return NULL;
   }
   if (!strcasecmp (suffix, "connect"))
   {
      b.ha = 1;
      return NULL;
   }
   return NULL;
}

void
send_ha_config (void)
{
   b.ha = 0;
   // TODO
}

void
revk_web_extra (httpd_req_t * req, int page)
{
   if (micws.set)
   {
      revk_web_setting (req, NULL, "micgain");
      revk_web_setting (req, NULL, "micstereo");
      if (!micstereo)
         revk_web_setting (req, NULL, "micright");
   }
   if (sdss.set)
   {
      revk_web_setting (req, NULL, "sdrectime");
      revk_web_setting (req, NULL, "wifilock");
   }
   if (vbus.set)
      revk_web_setting (req, NULL, "wifiusb");
   if (micws.set || spklrc.set)
   {
      revk_web_setting (req, NULL, "siphost");
      revk_web_setting (req, NULL, "sipuser");
      revk_web_setting (req, NULL, "sippass");
   }
   if (spklrc.set)
   {
      revk_web_setting (req, NULL, "morsestart");
   }
}

static void
register_uri (const httpd_uri_t * uri_struct)
{
   esp_err_t res = httpd_register_uri_handler (webserver, uri_struct);
   if (res != ESP_OK)
   {
      ESP_LOGE (TAG, "Failed to register %s, error code %d", uri_struct->uri, res);
   }
}

static void
register_get_uri (const char *uri, esp_err_t (*handler) (httpd_req_t * r))
{
   httpd_uri_t uri_struct = {
      .uri = uri,
      .method = HTTP_GET,
      .handler = handler,
   };
   register_uri (&uri_struct);
}

static esp_err_t
web_root (httpd_req_t * req)
{
   if (revk_link_down ())
      return revk_web_settings (req);   // Direct to web set up
   revk_web_head (req, "Audio");
   revk_web_send (req, "<h1>%s</h1>", *hostname ? hostname : appname);
   if (wifilock && b.sdpresent)
      revk_web_send (req, "<p>For security reasons, settings are disabled whilst the SD card is inserted</p>");
   return revk_web_foot (req, 0, 1, NULL);
}

SemaphoreHandle_t sd_mutex = NULL;

void
sd_task (void *arg)
{
   esp_err_t e = 0;
   revk_gpio_input (sdcd);
   sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT ();
   slot_config.gpio_cs = -1;    // don't use SS pin
   revk_gpio_output (sdss, 0);  // We assume only one card
   sdmmc_host_t host = SDSPI_HOST_DEFAULT ();
   //host.max_freq_khz = SDMMC_FREQ_PROBING;
   host.max_freq_khz = 20000;;
   spi_bus_config_t bus_cfg = {
      .mosi_io_num = sdmosi.num,
      .miso_io_num = sdmiso.num,
      .sclk_io_num = sdsck.num,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
   };
   e = spi_bus_initialize (host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
   if (e != ESP_OK)
   {
      rgbsd = 'R';
      jo_t j = jo_object_alloc ();
      jo_string (j, "error", cardstatus = "SPI failed");
      jo_int (j, "code", e);
      jo_int (j, "MOSI", sdmosi.num);
      jo_int (j, "MISO", sdmiso.num);
      jo_int (j, "CLK", sdsck.num);
      jo_int (j, "SS", sdss.num);
      revk_error ("SD", &j);
      vTaskDelete (NULL);
      return;
   }
   esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = 1,
      .max_files = 2,
      .allocation_unit_size = 16 * 1024,
      .disk_status_check_enable = 1,
   };
   sdmmc_card_t *card = NULL;
   slot_config.host_id = host.slot;
   while (!b.die)
   {
      if (sdcd.set)
      {
         if (b.dodismount)
         {                      // Waiting card removed
            rgbsd = 'B';
            jo_t j = jo_object_alloc ();
            jo_string (j, "action", cardstatus = revk_shutting_down (NULL) ? "Card dismounted for shutdown" : "Remove card");
            revk_info ("SD", &j);
            b.dodismount = 0;
            while ((b.sdpresent = revk_gpio_get (sdcd)))
               sleep (1);
            continue;
         }
         if (!(b.sdpresent = revk_gpio_get (sdcd)))
         {                      // No card
            jo_t j = jo_object_alloc ();
            jo_string (j, "error", cardstatus = "Card not present");
            revk_info ("SD", &j);
            rgbsd = 'M';
            if (wifilock)
            {
               revk_enable_ap ();
               revk_enable_settings ();
            }
            while (!(b.sdpresent = revk_gpio_get (sdcd)))
               sleep (1);
         }
         if (wifilock)
         {
            revk_disable_ap ();
            revk_disable_settings ();
         }
         b.sdpresent = 1;
      } else if (b.dodismount)
      {
         b.dodismount = 0;
         sleep (60);
         continue;
      }
      sleep (1);
      ESP_LOGI (TAG, "Mounting filesystem");
      e = esp_vfs_fat_sdspi_mount (sd_mount, &host, &slot_config, &mount_config, &card);
      if (e != ESP_OK)
      {
         jo_t j = jo_object_alloc ();
         if (e == ESP_FAIL)
            jo_string (j, "error", cardstatus = "Failed to mount");
         else
            jo_string (j, "error", cardstatus = "Failed to iniitialise");
         jo_int (j, "code", e);
         revk_error ("SD", &j);
         rgbsd = 'R';
         sleep (1);
         continue;
      }
      ESP_LOGI (TAG, "Filesystem mounted");
      b.sdpresent = 1;          // we mounted, so must be
      rgbsd = 'G';              // Writing to card
      if (b.doformat && (e = esp_vfs_fat_spiflash_format_rw_wl (sd_mount, "Audio")))
      {
         jo_t j = jo_object_alloc ();
         jo_string (j, "error", cardstatus = "Failed to format");
         jo_int (j, "code", e);
         revk_error ("SD", &j);
      }
      rgbsd = 'R';              // Oddly this call can hang forever!
      {
         esp_vfs_fat_info (sd_mount, &sdsize, &sdfree);
         jo_t j = jo_object_alloc ();
         jo_string (j, "action", cardstatus = (b.doformat ? "Formatted" : "Mounted"));
         jo_int (j, "size", sdsize);
         jo_int (j, "free", sdfree);
         revk_info ("SD", &j);
      }
      rgbsd = 'Y';              // Mounted, ready
      b.doformat = 0;
      uint32_t filesize = 0;
      while (!b.doformat && !b.dodismount && !b.die)
      {
         while (!b.doformat && !b.dodismount)
         {
            rgbsd = (sdfile ? 'G' : 'Y');
            if (!(b.sdpresent = revk_gpio_get (sdcd)))
            {                   // card removed
               b.dodismount = 1;
               break;
            }
            if (mic_mode == MIC_RECORD && !sdfile)
            {                   // Start file
               char filename[100];
               int fileno = 0;
               DIR *dir = opendir (sd_mount);
               if (dir)
               {
                  struct dirent *entry;
                  while ((entry = readdir (dir)))
                     if (entry->d_type == DT_REG)
                     {
                        const char *e = strrchr (entry->d_name, '.');
                        if (!e)
                           continue;
                        if (strcasecmp (e, ".wav"))
                           continue;
                        int n = atoi (entry->d_name);
                        if (n > fileno)
                           fileno = n;
                     }
                  closedir (dir);
               }
               fileno++;
               time_t now = time (0);
               struct tm t;
               localtime_r (&now, &t);
               if (t.tm_year >= 100)
                  sprintf (filename, "%s/%04d-%04d%02d%02dT%02d%02d%02d.WAV", sd_mount, fileno, t.tm_year + 1900, t.tm_mon + 1,
                           t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
               else
                  sprintf (filename, "%s/%04d.WAV", sd_mount, fileno);
               FILE *o = fopen (filename, "w+");
               if (!o)
                  ESP_LOGE (TAG, "Failed to open file %s", filename);
               else
               {
                  ESP_LOGI (TAG, "Recording opened %s", filename);
                  filesize = sdrectime * micfreq * micchannels * micbytes;
                  struct
                  {
                     char filetypeblocid[4];
                     uint32_t filesize;
                     char fileformatid[4];
                     char formatblocid[4];
                     uint32_t blocsize;
                     uint16_t audioformat;
                     uint16_t nbrchannels;
                     uint32_t frequency;
                     uint32_t bytepersec;
                     uint16_t byteperbloc;
                     uint16_t bitspersample;
                     char datablocid[4];
                     uint32_t datasize;
                  } riff = {
                     "RIFF",    // Master
                     36 + filesize,
                     "WAVE",
                     "fmt ",    // Chunk
                     16,
                     1,         // PCM
                     micchannels,
                     micfreq,
                     micfreq * micchannels * micbytes,
                     micchannels * micbytes,
                     micbytes * 8,      // bits
                     "data",    // Data block
                     filesize,
                  };
                  filesize += 44;
                  fwrite (&riff, sizeof (riff), 1, o);
                  sdfile = o;
               }
            }
            if (sdfile && sdin == sdout && (mic_mode != MIC_RECORD || ftell (sdfile) >= filesize))
            {                   // End file
               ESP_LOGI (TAG, "Recording closed");
               xSemaphoreTake (sd_mutex, portMAX_DELAY);
               FILE *o = sdfile;
               sdfile = NULL;
               xSemaphoreGive (sd_mutex);
               // Rewind and set size
               uint32_t len = ftell (o) - 44;   // Data len
               fseek (o, 40, SEEK_SET);
               fwrite (&len, 4, 1, o);
               len += 36;
               fseek (o, 4, SEEK_SET);
               fwrite (&len, 4, 1, o);
               fclose (o);
            }
            if (sdfile)
            {
               while (sdin != sdout)
               {
                  uint64_t a = esp_timer_get_time ();
                  fwrite (micaudio[sdout], 1, micsamples * micchannels * micbytes, sdfile);
                  uint64_t b = esp_timer_get_time ();
                  ESP_LOGE (TAG, "Wrote block %d, %ld bytes, %lldms", sdout, micsamples * micchannels * micbytes,
                            (b - a) / 1000ULL);
                  sdout = (sdout + 1) % MICQUEUE;
               }
            }
            usleep (10000);
         }
         rgbsd = 'B';
         // All done, unmount partition and disable SPI peripheral
         esp_vfs_fat_sdcard_unmount (sd_mount, card);
         ESP_LOGI (TAG, "Card dismounted");
         {
            jo_t j = jo_object_alloc ();
            jo_string (j, "action", cardstatus = "Dismounted");
            revk_info ("SD", &j);
         }
      }
   }
   revk_gpio_set (sdss, 1);
   rtc_gpio_set_direction_in_sleep (sdss.num, RTC_GPIO_MODE_OUTPUT_ONLY);
   rtc_gpio_set_level (sdss.num, 1 - sdss.invert);
   vTaskDelete (NULL);
}

void
mic_task (void *arg)
{
   jo_t e (esp_err_t err, const char *msg)
   {                            // Error
      jo_t j = jo_object_alloc ();
      if (msg)
         jo_string (j, "message", msg);
      if (err)
         jo_string (j, "error", esp_err_to_name (err));
      if (micdata.set)
         jo_int (j, "data", micdata.num);
      if (micclock.set)
         jo_int (j, "clock", micclock.num);
      return j;
   }
   while (!b.die)
   {                            // Loop here as we restart for SIP on/off
      mic_mode_t mode = MIC_IDLE;
      if (sip_mode > SIP_REGISTERED)
         mode = MIC_SIP;
      else if (b.micon)
         mode = MIC_RECORD;
      if (!mode)
      {
         usleep (100000);
         continue;
      }
      esp_err_t err;
      i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG (I2S_NUM_AUTO, I2S_ROLE_MASTER);
      if (b.sharedi2s)
         err = i2s_new_channel (&chan_cfg, &spk_handle, &mic_handle);   // Shared
      else
         err = i2s_new_channel (&chan_cfg, NULL, &mic_handle);
      uint8_t rawbytes = (micws.set ? micgain ? 4 : 3 : 2);     // No WS means PDM (16 bit)
      if (sip_mode > SIP_REGISTERED)
      {
         micfreq = SIP_RATE;
         micchannels = 1;
         micbytes = 2;
         micsamples = SIP_BYTES;
      } else
      {
         micfreq = micrate;
         micchannels = (micstereo ? 2 : 1);
         micbytes = 2;
         micsamples = micfreq * MICMS / 1000;
      }
      for (int i = 0; i < MICQUEUE; i++)
         micaudio[i] = mallocspi (micchannels * micbytes * micsamples);
      uint8_t *raw = NULL;
      if (micbytes != rawbytes)
         raw = mallocspi (micchannels * rawbytes * micsamples);
      if (micws.set)
      {                         // 24 bit Philips format
         ESP_LOGE (TAG, "Mic init CLK %d DAT %d WS %d", micclock.num, micdata.num, micws.num);
         i2s_std_config_t cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG (micfreq),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG ((rawbytes == 4 ? I2S_DATA_BIT_WIDTH_32BIT :        //
                                                              rawbytes == 3 ? I2S_DATA_BIT_WIDTH_24BIT :        //
                                                              I2S_DATA_BIT_WIDTH_16BIT),
                                                             (micchannels == 2 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO)),
            .gpio_cfg = {
                         .mclk = I2S_GPIO_UNUSED,
                         .bclk = micclock.num,
                         .ws = micws.num,
                         .dout = I2S_GPIO_UNUSED,
                         .din = micdata.num,
                         .invert_flags = {
                                          .mclk_inv = false,
                                          .bclk_inv = micclock.invert,
                                          .ws_inv = micws.invert,
                                          },
                         },
         };
         if (b.sharedi2s)
            cfg.gpio_cfg.dout = spkdata.num;    // Shared
         cfg.slot_cfg.slot_mask = (micchannels == 2 ? I2S_STD_SLOT_BOTH : micright ? I2S_STD_SLOT_RIGHT : I2S_STD_SLOT_LEFT);
         if (rawbytes == 3)
            cfg.clk_cfg.mclk_multiple = 384;
         if (!err)
            err = i2s_channel_init_std_mode (mic_handle, &cfg);
      } else
      {                         // PDM 16 bit
         ESP_LOGE (TAG, "Mic init PDM CLK %d DAT %d", micclock.num, micdata.num);
         i2s_pdm_rx_config_t cfg = {
            .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG (micfreq),
            .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG (I2S_DATA_BIT_WIDTH_16BIT,
                                                        (micchannels == 2 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO)),
            .gpio_cfg = {
                         .clk = micclock.num,
                         .din = micdata.num,
                         .invert_flags = {
                                          .clk_inv = micclock.invert}
                         }
         };
         cfg.slot_cfg.slot_mask = (micchannels == 2 ? I2S_PDM_SLOT_BOTH : micright ? I2S_PDM_SLOT_RIGHT : I2S_PDM_SLOT_LEFT);
         if (!err)
            err = i2s_channel_init_pdm_rx_mode (mic_handle, &cfg);
      }
      gpio_pulldown_en (micdata.num);
      if (!err)
         err = i2s_channel_enable (mic_handle);
      if (err)
      {
         ESP_LOGE (TAG, "Mic I2S failed");
         jo_t j = e (err, "Failed init I2S");
         revk_error ("i2s", &j);
         vTaskDelete (NULL);
         return;
      }
      mic_mode = mode;
      ESP_LOGE (TAG, "Mic started mode %d, %ld*%d*%d bits at %ldHz - mapped to %d*%d bits", mode, micsamples, micchannels,
                rawbytes * 8, micfreq, micchannels, micbytes * 8);
      while (!b.die && !(sip_mode <= SIP_REGISTERED && !b.micon))
      {
         size_t n = 0;
         i2s_channel_read (mic_handle, raw ? : micaudio[sdin], micchannels * rawbytes * micsamples, &n, 100);
         if (n < micchannels * rawbytes * micsamples)
            continue;
         if (raw)
         {                      // Process to 16 bits
            if (rawbytes != 4 || micbytes != 2)
            {
               ESP_LOGE (TAG, "Not coded %d->%d", rawbytes, micbytes);
               continue;
            }
            int32_t *i = (void *) raw;
            int16_t *o = (void *) micaudio[sdin];
            int s = micchannels * micsamples;
            while (s--)
            {
               int32_t v = ((*i++) / 256 * (micgain ? : 1)) / 256;
               if (v > 32767)
                  v = 32767;
               else if (v < -32768)
                  v = -32768;
               *o++ = v;
            }
         }
         switch (mic_mode)
         {
         case MIC_SIP:
            {
               int16_t *i = (void *) micaudio[sdin];
               uint8_t *o = (void *) micaudio[sdin];
               for (int s = 0; s < micsamples; s++)
                  *o++ = sip_pcm13_to_rtp[(*i++) / 8 + 4096];
               sip_audio (micsamples, (void *) micaudio[sdin]);
            }
            break;
         case MIC_RECORD:
            if ((sdin + 1) % MICQUEUE == sdout)
               ESP_LOGE (TAG, "Mic overflow");
            else
               sdin = (sdin + 1) % MICQUEUE;
            break;
         default:
         }
      }
      mic_mode = MIC_IDLE;
      i2s_channel_disable (mic_handle);
      free (raw);
      for (int i = 0; i < MICQUEUE; i++)
         free (micaudio[i]);
      i2s_del_channel (mic_handle);
      ESP_LOGE (TAG, "Mic stopped");
   }
   vTaskDelete (NULL);
}

void
spk_task (void *arg)
{
   ESP_LOGE (TAG, "Spk init BCLK %d DAT %d LR %d", spkbclk.num, spkdata.num, spklrc.num);
   if (!morsemessage && *morsestart)
      morsemessage = strdup (morsestart);
   while (!b.die)
   {
      const char *morsep = NULL;
      const char *tonep = NULL;
      spk_mode_t mode = SPK_IDLE;
      if (sip_mode > SIP_REGISTERED)
         mode = SPK_SIP;
      else if (morsemessage || tones)
         mode = SPK_TONE;
      // TODO WAV mode
      if (!mode)
      {                         // Speaker not needed
         usleep (100000);
         continue;
      }
      revk_gpio_output (spkpwr, 1);     // Power on
      esp_err_t e = 0;
      if (mode == SPK_WAV)
      {
         // TODO rate depends on WAV format
         if (b.sharedi2s)
            spkfreq = micrate;  // Shared with mic - so not going to work with playing WAV file
         else
            spkfreq = 8000;     // TODO
         spkchannels = 1;       // This gets complicated later if not 1
         spkbytes = 2;          // This gets complicated later if not 2, so needs some though
         spksamples = spkfreq * SPKMS / 1000;
      } else
      {
         spkfreq = SIP_RATE;
         spkchannels = 1;
         spkbytes = 2;
         spksamples = SIP_BYTES;
      }
      if (b.sharedi2s)
         spk_handle = mic_handle;
      else
      {
         i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG (I2S_NUM_AUTO, I2S_ROLE_MASTER);
         e = i2s_new_channel (&chan_cfg, &spk_handle, NULL);
         i2s_std_config_t cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG (spkfreq),
            .slot_cfg =
               I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG (sizeof (audio_t) == 1 ? I2S_DATA_BIT_WIDTH_8BIT : sizeof (audio_t) ==
                                                    2 ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                         .mclk = I2S_GPIO_UNUSED,
                         .bclk = spkbclk.num,
                         .ws = spklrc.num,
                         .dout = spkdata.num,
                         .din = I2S_GPIO_UNUSED,
                         .invert_flags = {
                                          .mclk_inv = false,
                                          .bclk_inv = spkbclk.invert,
                                          .ws_inv = spklrc.invert,
                                          },
                         },
         };
         if (!e)
            e = i2s_channel_init_std_mode (spk_handle, &cfg);
      }
      if (!e)
         e = i2s_channel_enable (spk_handle);
      if (e)
      {
         ESP_LOGE (TAG, "Spk I2S failed");
         jo_t j = jo_object_alloc ();
         jo_string (j, "error", esp_err_to_name (e));
         revk_error ("spk", &j);
         vTaskDelete (NULL);
         return;
      }
      spk_mode = mode;
      ESP_LOGE (TAG, "Spk started mode %d, %ld*%d*%d bits at %ldHz", mode, spksamples, spkchannels, spkbytes * 8, spkfreq);
      audio_t *samples = mallocspi (spkchannels * spkbytes * spksamples);
      uint32_t on = 1,
         off = 0,
         phase1 = 0,
         phase2 = 0,
         freq1 = 0,
         freq2 = 0,
         morseu = 0,            // Timings for morse
         morsef = 0,
         dtmfu = 0,             // Timings for DTMF
         dtmfg = 0;
      uint32_t level1 = 0,
         level2 = 0;
      int32_t tablesin (uint32_t p)
      {                         // 8kHz SIN
         if (p > SIP_RATE / 2)
            return -tablesin (p - SIP_RATE / 2);
         if (p > SIP_RATE / 4)
            return tablesin (SIP_RATE / 2 - p);
         return sip_sin4_8k[p];
      }
      if (mode == SPK_SIP)
      {
         level1 = 50;           // Ring tone
         freq1 = 400;
         level2 = 50;
         freq2 = 450;
      }
      if (mode == SPK_TONE)
      {
         if (morsemessage)
            morsep = morsemessage;      // New message
         else if (tones)
            tonep = tones;      // New tones
         morseu = 60 * spkfreq / morsewpm / 50;
         morsef = (60 * spkfreq / morsefwpm - 31 * morseu) / 19;
         dtmfu = dtmftone * spkfreq / 1000;
         dtmfg = dtmfgap * spkfreq / 1000;
      }
      while (!b.die && mode)
      {
         switch (mode)
         {
         case SPK_TONE:
            {
               for (int i = 0; i < spksamples; i++)
               {
                  samples[i] = 0;
                  if (on)
                  {
                     if (freq2)
                     {
                        samples[i] = (tablesin (phase1) * (int) level1 + tablesin (phase2) * (int) level2) / 100 / 4;
                        phase2 += freq2;
                        if (phase2 >= SIP_RATE)
                           phase2 -= SIP_RATE;
                     } else
                        samples[i] = tablesin (phase1) * (int) level1 / 100 / 2;
                     phase1 += freq1;
                     if (phase1 >= SIP_RATE)
                        phase1 -= SIP_RATE;
                     on--;
                     continue;
                  }
                  if (off)
                  {
                     off--;
                     continue;
                  }
                  if (!tonep || !*tonep)
                  {
                     tonep = NULL;
                     free (tones);
                     tones = NULL;
                     if (morsep)
                     {          // More morse
                        if (!*morsep)
                        {       // End of morse
                           morsep = NULL;
                           free (morsemessage);
                           morsemessage = NULL;
                           off = morsef * 7;    // Word gap
                           continue;
                        }
                        char c = toupper ((int) *morsep++);
                        for (int i = 0; i < sizeof (morse) / sizeof (*morse); i++)
                           if (morse[i].c == c)
                           {
                              tonep = tones = strdup (morse[i].m);
                              break;
                           }
                        if (!tones)
                        {
                           off = morsef * 7;    // Word gap
                           continue;
                        }
                        off = morsef * 3;       // inter character
                        continue;
                     }
                     if (!tones)
                     {          // End
                        if (morsep)
                           off = morsef * 7 - morseu;   // Word gap (we did morseu already)
                        else
                        {
                           off = spkfreq;       // Long gap
                           mode = SPK_IDLE;
                        }
                     }
                     continue;
                  }
                  if (*tonep == '.')
                  {             // Morse dot
                     level1 = morselevel;
                     on = morseu;
                     off = morseu;
                     freq1 = morsefreq;
                     freq2 = 0;
                  } else if (*tonep == '-')
                  {             // Morse dash
                     level1 = morselevel;
                     on = morseu * 3;
                     off = morseu;
                     freq1 = morsefreq;
                     freq2 = 0;
                  } else
                  {             // DTMF
                     level2 = level1 = dtmflevel;
                     off = dtmfg;
                     static const char dtmf[] = "123A456B789C*0#D";
                     static const uint32_t col[] = { 1209, 1336, 1477, 1633 };
                     static const uint32_t row[] = { 697, 770, 852, 941 };
                     const char *p = strchr (dtmf, *tonep);
                     if (p)
                     {
                        freq1 = col[(p - dtmf) % 4];
                        freq2 = row[(p - dtmf) / 4];
                        on = dtmfu;
                     }
                  }
                  tonep++;
               }
               size_t l = 0;
               i2s_channel_write (spk_handle, samples, spkbytes * spkchannels * spksamples, &l, 100);
            }
            break;
         case SPK_SIP:
            if (sip_mode <= SIP_REGISTERED)
               mode = SIP_IDLE;
            else if (sip_mode == SIP_IC_ALERT)
            {
               // TODO this is continuous, needs cycling as normal ringing tone
               for (int i = 0; i < spksamples; i++)
               {
                  samples[i] = (tablesin (phase1) * (int) level1 + tablesin (phase2) * (int) level2) / 100 / 4;
                  phase1 += freq1;
                  if (phase1 >= SIP_RATE)
                     phase1 -= SIP_RATE;
                  phase2 += freq2;
                  if (phase2 >= SIP_RATE)
                     phase2 -= SIP_RATE;
               }
               size_t l = 0;
               i2s_channel_write (spk_handle, samples, spkbytes * spkchannels * spksamples, &l, 100);
            }
            break;
         default:
         }
      }
      spk_mode = SPK_IDLE;
      ESP_LOGE (TAG, "Spk stopped");
      revk_gpio_output (spkpwr, 0);
      if (!b.sharedi2s)
      {
         i2s_channel_disable (spk_handle);
         i2s_del_channel (spk_handle);
      }
   }
   rtc_gpio_set_direction_in_sleep (spkpwr.num, RTC_GPIO_MODE_OUTPUT_ONLY);
   rtc_gpio_set_level (spkpwr.num, sdss.invert);
   vTaskDelete (NULL);
}

void
sip_callback (sip_state_t state, uint8_t len, const uint8_t * data)
{
   if (sip_mode != state)
   {
      sip_mode = state;
      ESP_LOGE (TAG, "SIP state %d", state);
      if (state == SIP_IC_ALERT)
      {
         if (spk_mode || mic_mode)
            sip_hangup ();
         else if (!button.set || !spklrc.set)
            sip_answer ();
      }
   }
   if (data && len == SIP_BYTES && spk_mode == SPK_SIP && (state == SIP_IC || state == SIP_OG || state == SIP_OG_ALERT))
   {
      int16_t samples[SIP_BYTES];
      for (int i = 0; i < SIP_BYTES; i++)
         samples[i] = sip_rtp_to_pcm13[data[i]] * 8;
      size_t l = 0;
      i2s_channel_write (spk_handle, samples, SIP_BYTES * 2, &l, 100);
   }
}

void
app_main ()
{
   sd_mutex = xSemaphoreCreateBinary ();
   xSemaphoreGive (sd_mutex);
   revk_boot (&app_callback);
   revk_start ();
   if (micws.num == spklrc.num && micclock.num == spkbclk.num)
      b.sharedi2s = 1;
   httpd_config_t config = HTTPD_DEFAULT_CONFIG ();     // When updating the code below, make sure this is enough
   //  Note that we 're also 4 adding revk' s web config handlers
   config.max_uri_handlers = 8;
   if (!httpd_start (&webserver, &config))
   {
      revk_web_settings_add (webserver);
      register_get_uri ("/", web_root);
   }
   led_strip_handle_t led_status = NULL;
   if (rgbstatus.set)
   {
      led_strip_config_t strip_config = {
         .strip_gpio_num = rgbstatus.num,
         .max_leds = 1,
         .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
         .led_model = LED_MODEL_WS2812, // LED strip model
         .flags.invert_out = rgbstatus.invert,
      };
      led_strip_rmt_config_t rmt_config = {
         .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
         .resolution_hz = 10 * 1000 * 1000,     // 10 MHz
      };
      REVK_ERR_CHECK (led_strip_new_rmt_device (&strip_config, &rmt_config, &led_status));
   }
   led_strip_handle_t led_record = NULL;
   if (rgbrecord.set)
   {
      led_strip_config_t strip_config = {
         .strip_gpio_num = rgbrecord.num,
         .max_leds = 1,
         .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
         .led_model = LED_MODEL_WS2812, // LED strip model
         .flags.invert_out = rgbrecord.invert,
      };
      led_strip_rmt_config_t rmt_config = {
         .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
         .resolution_hz = 10 * 1000 * 1000,     // 10 MHz
      };
      REVK_ERR_CHECK (led_strip_new_rmt_device (&strip_config, &rmt_config, &led_record));
   }
   // Tasks
   if (spklrc.set && spkbclk.set && spkdata.set)
      revk_task ("spk", spk_task, NULL, 8);
   if (micdata.set && micclock.set)
      revk_task ("mic", mic_task, NULL, 8);
   if (sdss.set && sdmosi.set && sdmiso.set && sdsck.set)
      revk_task ("sd", sd_task, NULL, 16);

   if (*siphost)
      sip_register (siphost, sipuser, sippass, sip_callback);

   // Buttons and LEDs
   revk_gpio_input (button);
   revk_gpio_input (charging);  // On, off, or flashing
   revk_gpio_input (vbus);      // USB status
   uint8_t press = 255;
   uint8_t charge = 0;
   uint8_t usb = 1;
   while (!b.die)
   {
      usleep (100000);
      if (b.ha)
         send_ha_config ();
      if (vbus.set)
      {
         usb = revk_gpio_get (vbus);
         if (usb != b.usb)
         {
            b.usb = usb;
            if (wifiusb)
            {
               if (usb)
                  revk_enable_wifi ();
               else
                  revk_disable_wifi ();
            }
         }
      }
      if (charging.set)
      {
         charge = (charge << 1) | revk_gpio_get (charging);
         revk_blink (0, 0, charge == 0xFF ? "Y" : !charge ? "R" : "G");
      }
      if (revk_gpio_get (button))
      {                         // Pressed
         if (press < 255)
            press++;
         if (press == 30 && rtc_gpio_is_valid_gpio (button.num))
            b.die = 1;          // Long press - shutdown (if we can wake up later)S
         // TODO call hangup if ic alerting
      } else if (press)
      {                         // Released
         if (press < 255)
         {
            if (sip_mode == SIP_IC_ALERT)
               sip_answer ();
            else if (sip_mode == SIP_IC || sip_mode == SIP_OG || sip_mode == SIP_OG_ALERT)
               sip_hangup ();
            else if (!b.micon && !b.sdpresent)
               ESP_LOGE (TAG, "No card");
            else
               b.micon = 1 - b.micon;
         }
         press = 0;
      }
      if (led_status)
      {
         revk_led (led_status, 0, 255, revk_blinker ());
         REVK_ERR_CHECK (led_strip_refresh (led_status));
      }
      if (led_record)
      {
         revk_led (led_record, 0, 255, revk_rgb (rgbsd));
         REVK_ERR_CHECK (led_strip_refresh (led_record));
      }
   }
   // Go dark
   if (led_status)
   {
      revk_led (led_status, 0, 255, 0);
      REVK_ERR_CHECK (led_strip_refresh (led_status));
   }
   if (led_record)
   {
      revk_led (led_record, 0, 255, 0);
      REVK_ERR_CHECK (led_strip_refresh (led_record));
   }
   // Alarm
   if (button.set && rtc_gpio_is_valid_gpio (button.num))
   {
      rtc_gpio_set_direction_in_sleep (button.num, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en (button.num);
      rtc_gpio_pulldown_dis (button.num);
      REVK_ERR_CHECK (esp_sleep_enable_ext0_wakeup (button.num, 1 - button.invert));
   }
   revk_disable_wifi ();
   // Shutdown
   sleep (1);                   // Allow tasks to end
   esp_deep_sleep_start ();     // Night night
}
