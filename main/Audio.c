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
#include <esp_http_server.h>
#include "fft.h"
#include "math.h"
#include "esp_vfs_fat.h"
#include <sys/dirent.h>

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
char *dtmfmessage = NULL;       // Malloc'd

struct
{
   uint8_t die:1;               // Shutting down
   uint8_t sdpresent:1;         // SD present
   uint8_t doformat:1;          // SD format
   uint8_t dodismount:1;        // Dismount SD
   uint8_t micon:1;             // Sounds required
   uint8_t button:1;            // Last button state
} b;
const char sd_mount[] = "/sd";
char rgbsd = 0;                 // Colour for SD card
const char *cardstatus = NULL;  // Status of SD card
uint64_t sdsize = 0,            // SD card data
   sdfree = 0;
FILE *volatile sdfile = NULL;
#define	MICMS		20
#define	MICQUEUE	32
uint8_t micbytes = 0;
uint32_t micsamples = 0;
uint8_t *micaudio[MICQUEUE] = { 0 };

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
   if (!strcasecmp (suffix, "dtmf"))
   {
      if (dtmfmessage)
         return "Wait";
      if (jo_here (j) != JO_STRING)
         return "JSON string";
      // TODO allow object with custom timings, and so on
      dtmfmessage = jo_strdup (j);
      return NULL;
   }
   if (!strcasecmp (suffix, "format"))
   {
      b.doformat = 1;
      return NULL;
   }
   return NULL;
}

void
revk_web_extra (httpd_req_t * req, int page)
{
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
   return revk_web_foot (req, 0, 1, NULL);
}

SemaphoreHandle_t sd_mutex = NULL;

void
sd_task (void *arg)
{
   esp_err_t e = 0;
   revk_gpio_input (sdcd);
   sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT ();
#if 0
   slot_config.gpio_cs = sdss.num;      // use SS pin
#else
   slot_config.gpio_cs = -1;    // don't use SS pin
   revk_gpio_output (sdss, 0);  // Bodge for faster SD card access in ESP IDF V5+
#endif
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
            revk_enable_wifi ();
            revk_enable_ap ();
            while (!(b.sdpresent = revk_gpio_get (sdcd)))
               sleep (1);
         }
         if (!wifidebug)
            revk_disable_wifi ();
         revk_disable_ap ();
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
            if (b.micon && !sdfile)
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
                  uint32_t onehour = 3600 * micrate * micbytes;
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
                     36 + onehour,
                     "WAVE",
                     "fmt ",    // Chunk
                     16,
                     1,         // PCM
                     2,         // Stereo
                     micrate,
                     micrate * micbytes,
                     micbytes,
                     micbytes * 4,      // bits
                     "data",    // Data block
                     onehour,
                  };
                  fwrite (&riff, sizeof (riff), 1, o);
                  sdfile = o;
               }
            }
            if (!b.micon && sdfile && sdin == sdout)
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
                  fwrite (micaudio[sdout], 1, micsamples * micbytes, sdfile);
                  uint64_t b = esp_timer_get_time ();
                  if ((b - a) / 1000ULL > MICMS)
                     ESP_LOGE (TAG, "Wrote block %d, %ld bytes, %lldms", sdout, micsamples * micbytes, (b - a) / 1000ULL);
                  sdout = (sdout + 1) % MICQUEUE;
               }
            }
            usleep (MICMS * 1000);
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
   esp_err_t err;
   i2s_chan_handle_t i = { 0 };
   i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG (I2S_NUM_AUTO, I2S_ROLE_MASTER);
   err = i2s_new_channel (&chan_cfg, NULL, &i);
   uint8_t rawbytes = (micws.set ? 8 : 4);      // No WS means PDM (16 bit)
   micbytes = 4;
   micsamples = micrate * MICMS / 1000;
   for (int i = 0; i < MICQUEUE; i++)
      micaudio[i] = mallocspi (micbytes * micsamples);
   uint8_t *raw = NULL;
   if (micbytes != rawbytes)
      raw = mallocspi (rawbytes * micsamples);
   if (micws.set)
   {                            // 24 bit Philips format
      ESP_LOGE (TAG, "Mic init CLK %d DAT %d WS %d", micclock.num, micdata.num, micws.num);
      i2s_std_config_t cfg = {
         .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG (micrate),
         .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG ((rawbytes == 8 ? I2S_DATA_BIT_WIDTH_32BIT : rawbytes ==
                                                 6 ? I2S_DATA_BIT_WIDTH_24BIT : I2S_DATA_BIT_WIDTH_16BIT), I2S_SLOT_MODE_STEREO),
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
      cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
      if (rawbytes == 6)
         cfg.clk_cfg.mclk_multiple = 384;
      if (!err)
         err = i2s_channel_init_std_mode (i, &cfg);
   } else
   {                            // PDM 16 bit
      ESP_LOGE (TAG, "Mic init PDM CLK %d DAT %d", micclock.num, micdata.num);
      i2s_pdm_rx_config_t cfg = {
         .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG (micrate),
         .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG (I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
         .gpio_cfg = {
                      .clk = micclock.num,
                      .din = micdata.num,
                      .invert_flags = {
                                       .clk_inv = micclock.invert}
                      }
      };
      cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_BOTH;
      if (!err)
         err = i2s_channel_init_pdm_rx_mode (i, &cfg);
   }
   gpio_pulldown_en (micdata.num);
   if (!err)
      err = i2s_channel_enable (i);
   if (err)
   {
      ESP_LOGE (TAG, "Mic I2S failed");
      jo_t j = e (err, "Failed init I2S");
      revk_error ("i2s", &j);
      vTaskDelete (NULL);
      return;
   }
   ESP_LOGE (TAG, "Mic started, %ld*%d bits at %ldHz", micsamples, rawbytes * 8, micrate);
   while (1)
   {
      size_t n = 0;
      i2s_channel_read (i, raw ? : micaudio[sdin], rawbytes * micsamples, &n, 100);
      if (n < rawbytes * micsamples)
         continue;
      if (rawbytes != micbytes)
      {                         // Copy
         if (rawbytes != 8 || micbytes != 4)
         {
            ESP_LOGE (TAG, "Not coded %d->%d", rawbytes, micbytes);
            continue;
         }
         int32_t *i = (void *) raw;
         int16_t *o = (void *) micaudio[sdin];
         int s = micsamples * 2;
         while (s--)
            *o++ = (micgain ** i++) / 65536;
      }
      if (!b.micon || !sdfile)
         continue;              // Not needed
      if ((sdin + 1) % MICQUEUE == sdout)
         ESP_LOGE (TAG, "Mic overflow");
      else
         sdin = (sdin + 1) % MICQUEUE;
   }
   vTaskDelete (NULL);
}

void
spk_task (void *arg)
{
   esp_err_t e = 0;
   i2s_chan_handle_t tx_handle;
   i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG (I2S_NUM_AUTO, I2S_ROLE_MASTER);
   if (!e)
      e = i2s_new_channel (&chan_cfg, &tx_handle, NULL);

   i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG (spkrate),
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
      e = i2s_channel_init_std_mode (tx_handle, &std_cfg);
   if (!e)
      e = i2s_channel_enable (tx_handle);
   if (e)
   {
      ESP_LOGE (TAG, "Spk I2S failed");
      jo_t j = jo_object_alloc ();
      jo_string (j, "error", esp_err_to_name (e));
      revk_error ("spk", &j);
      vTaskDelete (NULL);
      return;
   }
#define	SAMPLES	(spkrate/10)
   if (!morsemessage && *morsestart)
      morsemessage = strdup (morsestart);
   audio_t *samples = mallocspi (sizeof (audio_t) * SAMPLES);
   const char *morsemessagep = NULL;
   const char *dtmfmessagep = NULL;
   const char *dd = NULL;
   uint32_t on = 1,
      off = 0,
      freq1 = 0,
      freq2 = 0,
      phase1 = 0,
      phase2 = 0,
      unit1 = 0,
      unit2 = 0;
   audio_t *sin4 = malloc (sizeof (audio_t) * (spkrate / 4 + 1));
   for (int i = 0; i < spkrate / 4 + 1; i++)
      sin4[i] = audio_max * sin (M_PI * i / spkrate / 2);
   int32_t tablesin (int p)
   {
      if (p > spkrate / 2)
         return -tablesin (p - spkrate / 2);
      if (p > spkrate / 4)
         return tablesin (spkrate / 2 - p);
      return sin4[p];
   }
   while (1)
   {
      size_t l = 0;
      if (!morsemessagep && !dtmfmessagep && morsemessage)
      {
         morsemessagep = morsemessage;  // New message
         unit1 = 60 * spkrate / morsewpm / 50;
         unit2 = (60 * spkrate / morsefwpm - 31 * unit1) / 19;
         freq1 = morsefreq;
      }
      if (!morsemessagep && !dtmfmessagep && dtmfmessage)
      {
         dtmfmessagep = dtmfmessage;    // New message
         unit1 = dtmftone * spkrate / 1000;
         unit2 = dtmfgap * spkrate / 1000;
      }
      if (morsemessagep)
         for (int i = 0; i < SAMPLES; i++)
         {
            if (on)
            {
               samples[i] = tablesin (phase1) * morselevel / 100;
               on--;
               phase1 += freq1;
               if (phase1 >= spkrate)
                  phase1 -= spkrate;
               continue;
            }
            if (off)
            {
               samples[i] = 0;
               off--;
               continue;
            }
            if (!dd)
            {                   // End of character
               if (morsemessagep && !*morsemessagep)
               {                // End of message
                  morsemessagep = NULL;
                  free (morsemessage);
                  morsemessage = NULL;
                  off = unit1;
                  continue;
               }
               if (!morsemessagep)
               {
                  off = unit1;
                  continue;
               }
               char c = toupper ((int) *morsemessagep);
               for (int i = 0; i < sizeof (morse) / sizeof (*morse); i++)
                  if (morse[i].c == c)
                  {
                     dd = morse[i].m;
                     break;
                  }
               morsemessagep++;
               if (!dd)
               {
                  off = unit2 * 7;
                  continue;
               }
            }
            if (*dd == '.')
               on = unit1;
            else if (*dd == '-')
               on = unit1 * 3;
            dd++;
            if (!*dd)
            {
               dd = NULL;
               off = unit2 * 3; // inter character
               if (morsemessagep && !*morsemessagep)
                  off = unit2 * 7;      // inter word
            } else
               off = unit1;     // intra character
      } else if (dtmfmessagep)
         for (int i = 0; i < SAMPLES; i++)
         {
            if (on)
            {
               samples[i] = (tablesin (phase1) + tablesin (phase2)) * dtmflevel / 100 / 2;
               on--;
               phase1 += freq1;
               if (phase1 >= spkrate)
                  phase1 -= spkrate;
               phase2 += freq2;
               if (phase2 >= spkrate)
                  phase2 -= spkrate;
               continue;
            }
            if (off)
            {
               samples[i] = 0;
               off--;
               continue;
            }
            if (!*dtmfmessagep)
            {
               dtmfmessagep = NULL;
               free (dtmfmessage);
               dtmfmessage = NULL;
               off = unit2;
               continue;
            }
            off = unit2;
            static const char dtmf[] = "123A456B789C*0#D";
            static const uint32_t col[] = { 1209, 1336, 1477, 1633 };
            static const uint32_t row[] = { 697, 770, 852, 941 };
            const char *p = strchr (dtmf, *dtmfmessagep);
            if (p)
            {
               freq1 = col[(p - dtmf) % 4];
               freq2 = row[(p - dtmf) / 4];
               on = unit1;
            }
            dtmfmessagep++;
      } else
         memset (samples, 0, sizeof (audio_t) * SAMPLES);       // Silence
      i2s_channel_write (tx_handle, samples, sizeof (audio_t) * SAMPLES, &l, 100);
   }

   vTaskDelete (NULL);
}

void
app_main ()
{
   sd_mutex = xSemaphoreCreateBinary ();
   xSemaphoreGive (sd_mutex);
   revk_boot (&app_callback);
   revk_start ();

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

   // Buttons and LEDs
   revk_gpio_input (button);
   revk_gpio_input (charging);
   while (1)
   {
      usleep (100000);
      uint8_t press = revk_gpio_get (button);
      if (press != b.button)
      {
         b.button = press;
         if (press)
         {
            if (!b.micon && !b.sdpresent)
               ESP_LOGE (TAG, "No card");
            else
               b.micon = 1 - b.micon;
         }
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
}
