/* OpenMic app */
/* Copyright ©2024 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static const char TAG[] = "OpenMic";

#include "revk.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include <netdb.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/i2c.h>
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <driver/rtc_io.h>
#include <driver/rmt_rx.h>
#include <driver/sdmmc_host.h>
#include "esp_http_client.h"
#include <esp_http_server.h>
#include "esp_crt_bundle.h"
#include "fft.h"
#include "math.h"
#include "esp_vfs_fat.h"
#include <sys/dirent.h>
#include <sip.h>
#include "halib.h"

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
   {'.', ".-.-.-"},
   {',', "--..--"},
   {'?', "..--.."},
   {'\'', ".---."},
   {'/', "-..-."},
   {'(', "-.--."},
   {')', "-.--.-"},
   {'&', ".-..."},
   {':', "---..."},
   {';', "-.-.-."},
   {'=', "-...-"},
   {'+', ".-.-."},
   {'-', "-....-"},
   {'+', "..--.-"},
   {'"', ".-..-."},
   {'$', "...-..-"},
   {'@', ".--.-."},
};

// TODO international characters

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
   uint8_t overrun:1;           // Record overrun
} b = { 0 };

//#define       SDSPI		// TODO use SPI mode
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
   if (suffix && ((haenable && (!strcmp (suffix, "connect") || !strcmp (suffix, "status"))) || !strcmp (suffix, "setting")))
   {
      b.ha = 1;
      return NULL;
   }
   if (!strcasecmp (suffix, "record"))
   {
      if (b.sdpresent && !jo_strcmp (j, "ON"))
         b.micon = 1;
      else
         b.micon = 0;
      return NULL;
   }
   return NULL;
}

void
send_ha_config (void)
{
   b.ha = 0;
 ha_config_switch ("record", name: "Record", cmd: "/record", field: "record", delete:(!haenable && !wifirecord));
}

void
revk_state_extra (jo_t j)
{
   if (vbus.set)
      jo_bool (j, "power", b.usb);
   if (sdcmd.set)
      jo_bool (j, "sdcard", b.sdpresent);
   if (micws.set)
      jo_string (j, "record", b.micon ? "ON" : "OFF");
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
   if (sdcmd.set)
   {
      if (micws.set && rgbled.set)
         revk_web_setting (req, NULL, "micbeep");
      revk_web_setting (req, NULL, "sdsynctime");
      revk_web_setting (req, NULL, "sdrectime");
      revk_web_setting (req, NULL, "sdupload");
      revk_web_setting (req, NULL, "sddelete");
      revk_web_setting (req, NULL, "haenable");
      revk_web_setting (req, NULL, "wifirecord");
      revk_web_setting (req, NULL, "wifilock");
   }
   if (vbus.set)
   {
      revk_web_setting (req, NULL, "wifiusb");
      if (autooff)
         revk_web_setting (req, NULL, "autousb");
   }
   revk_web_setting (req, NULL, "autooff");
   if (micws.set || spklrc.set)
   {
      revk_web_setting (req, NULL, "siphost");
      revk_web_setting (req, NULL, "sipuser");
      revk_web_setting (req, NULL, "sippass");
      revk_web_setting (req, NULL, "sipoutgoing");
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
register_post_uri (const char *uri, esp_err_t (*handler) (httpd_req_t * r))
{
   httpd_uri_t uri_struct = {
      .uri = uri,
      .method = HTTP_POST,
      .handler = handler,
   };
   register_uri (&uri_struct);
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
   revk_web_head (req, "OpenMic");
   revk_web_send (req, "<h1>%s</h1>", *hostname ? hostname : appname);
   const char *er = NULL;
   jo_t j = revk_web_query (req);
   if (j)
   {
      if (jo_find (j, "tone") || jo_find (j, "dtmf") || jo_find (j, "oa"))
      {
         if (tones)
            er = "Wait until finished";
         else
            tones = jo_strdup (j);
      }
      if (jo_find (j, "morse") || jo_find (j, "ud"))
      {
         if (morsemessage)
            er = "Wait until finished";
         else
            morsemessage = jo_strdup (j);
      }
      jo_free (&j);
   }
   if (spklrc.set)
      revk_web_send (req,
                     "<form method=post><table><tr><td>Morse</td><td><input autofocus name=morse size=80></td></tr><tr><td>DTMF</td><td><input name=tone></td></tr></table><input type=submit value=Send></form>");
   if (er)
      revk_web_send (req, "<p>%s</p>", er);
   if (wifilock && b.sdpresent)
      revk_web_send (req, "<hr><p>For security reasons, settings are disabled whilst the SD card is inserted</p>");
   if (b.overrun)
      revk_web_send (req, "<p>Recording overrun - use faster SD card</p>");
   return revk_web_foot (req, 0, 1, NULL);
}

SemaphoreHandle_t sd_mutex = NULL;

void
sd_task (void *arg)
{
   esp_err_t e = 0;
   revk_gpio_input (sdcd);
#ifdef	SDSPI
   // SPI mode
   sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT ();
   slot.gpio_cs = -1;           // don't use SS pin
   revk_gpio_output (sddat3, 0);        // We assume only one card
   sdmmc_host_t host = SDSPI_HOST_DEFAULT ();
   //host.max_freq_khz = SDMMC_FREQ_PROBING;
   host.max_freq_khz = 20000;;
   spi_bus_config_t bus_cfg = {
      .mosi_io_num = sdcmd.num,
      .miso_io_num = sddat0.num,
      .sclk_io_num = sdclk.num,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      //.max_transfer_sz = 4000,
   };
   e = spi_bus_initialize (host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
   if (e != ESP_OK)
   {
      rgbsd = 'R';
      jo_t j = jo_object_alloc ();
      jo_string (j, "error", cardstatus = "SPI failed");
      jo_int (j, "code", e);
      jo_int (j, "MOSI", sdcmd.num);
      jo_int (j, "MISO", sddat0.num);
      jo_int (j, "CLK", sdclk.num);
      jo_int (j, "SS", sddat3.num);
      revk_error ("SD", &j);
      vTaskDelete (NULL);
      return;
   }
   slot.host_id = host.slot;
#else
   sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT ();
   slot.clk = sdclk.num;
   slot.cmd = sdcmd.num;
   slot.d0 = sddat0.num;
   slot.d1 = sddat1.set ? sddat1.num : -1;
   slot.d2 = sddat2.set ? sddat2.num : -1;
   slot.d3 = sddat3.set ? sddat3.num : -1;
   //slot.cd = sdcd.set ? sdcd.num : -1;
   slot.width = (sddat2.set && sddat3.set ? 4 : sddat1.set ? 2 : 1);
   //slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP; // TODO, old boards?
   sdmmc_host_t host = SDMMC_HOST_DEFAULT ();
   host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
   host.slot = SDMMC_HOST_SLOT_1;
#endif
   esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = 1,
      .max_files = 2,
      .allocation_unit_size = 16 * 1024,
      .disk_status_check_enable = 1,
   };
   sdmmc_card_t *card = NULL;
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
      ESP_LOGD (TAG, "Mounting SD card");
#ifdef	SDSPI
      e = esp_vfs_fat_sdspi_mount (sd_mount, &host, &slot, &mount_config, &card);
#else
      e = esp_vfs_fat_sdmmc_mount (sd_mount, &host, &slot, &mount_config, &card);
#endif
      if (e != ESP_OK)
      {
         ESP_LOGE (TAG, "SD Mount failed");
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
      ESP_LOGE (TAG, "SD Card mounted");
      b.sdpresent = 1;          // we mounted, so must be
      rgbsd = 'G';              // Writing to card
#ifndef	SDSPI                   // TODO
      if (b.doformat && (e = esp_vfs_fat_spiflash_format_rw_wl (sd_mount, "OpenMic")))
      {
         jo_t j = jo_object_alloc ();
         jo_string (j, "error", cardstatus = "Failed to format");
         jo_int (j, "code", e);
         revk_error ("SD", &j);
      }
#endif
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
      uint32_t writebytes = 0;  // Bytes of actual data written
      uint32_t filesize = 0;    // End of file writebytes
      uint32_t filesync = 0;    // Sync next writebytes
      uint64_t writetime = 0;   // Total us writing
      char *filename = NULL;
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
               filesize = sdrectime * micfreq * micchannels * micbytes;
               filesync = sdsynctime * micfreq * micchannels * micbytes;
               int fileno = 0;
               char *oldest = NULL;
               DIR *dir = opendir (sd_mount);
               if (dir)
               {
                  struct dirent *entry;
                  while ((entry = readdir (dir)))
                     if (entry->d_type == DT_REG)
                     {
                        if (!isdigit ((int) *(unsigned char *) (entry->d_name)))
                           continue;
                        const char *e = strrchr (entry->d_name, '.');
                        if (!e || (strcasecmp (e, ".wav") && !strcasecmp (e, ".bad")))
                           continue;
                        int n = atoi (entry->d_name);
                        if (n > fileno)
                           fileno = n;
                        if (!oldest || atoi (entry->d_name) < atoi (oldest))
                        {
                           free (oldest);
                           oldest = strdup (entry->d_name);
                        }
                     }
                  if (sddelete && oldest)
                  {             // Do we need to delete oldest
                     esp_vfs_fat_info (sd_mount, &sdsize, &sdfree);
                     if (sdfree < filesize + 44 + 4096)
                     {
                        asprintf (&filename, "%s/%s", sd_mount, oldest);
                        ESP_LOGE (TAG, "Purge %s", filename);
                        unlink (filename);
                        free (oldest);
                        free (filename);
                        filename = NULL;
                     }
                  }
                  closedir (dir);
               }
               fileno++;
               time_t now = time (0);
               struct tm t;
               localtime_r (&now, &t);
               if (t.tm_year >= 100)
                  asprintf (&filename, "%s/%04d-%04d%02d%02dT%02d%02d%02d.WAV", sd_mount, fileno, t.tm_year + 1900, t.tm_mon + 1,
                            t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
               else
                  asprintf (&filename, "%s/%04d-.WAV", sd_mount, fileno);
               FILE *o = fopen (filename, "w+");
               if (!o)
                  ESP_LOGE (TAG, "Failed to open file %s", filename);
               else
               {
                  ESP_LOGE (TAG, "Recording opened %s sync %lu max %lu", filename, filesync, filesize);
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
                     36,        // Length zero
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
                     0,         // Length zero
                  };
                  fwrite (&riff, sizeof (riff), 1, o);
                  sdfile = o;
                  writetime = 0;
                  writebytes = 0;
               }
            }
            if (sdfile && (mic_mode != MIC_RECORD || writebytes >= filesize || writebytes >= filesync))
            {                   // End file
               // Rewind and set size
               fclose (sdfile);
               sdfile = fopen (filename, "r+");
               if (!sdfile)
                  ESP_LOGE (TAG, "Error %s", filename);
               else
               {
                  uint32_t len = writebytes;    // Data len
                  fseek (sdfile, 40, SEEK_SET);
                  fwrite (&len, 4, 1, sdfile);
                  len += 36;
                  fseek (sdfile, 4, SEEK_SET);
                  fwrite (&len, 4, 1, sdfile);
                  if (mic_mode == MIC_RECORD && writebytes < filesize)
                  {             // Just a sync
                     fseek (sdfile, 0, SEEK_END);
                     ESP_LOGE (TAG, "Sync %s at %lu", filename, writebytes);
                     filesync += sdsynctime * micfreq * micchannels * micbytes;
                  } else
                  {             // File sclose
                     ESP_LOGE (TAG, "Recording closed %s at %lu", filename, writebytes);
                     fclose (sdfile);
                     sdfile = NULL;
                     if (writetime)
                     {
                        ESP_LOGE (TAG, "%lu bytes %llums (%llukB/sec) %s", writebytes, writetime / 1000ULL,
                                  writebytes * 1000ULL / writetime, filename);
                        jo_t j = jo_object_alloc ();
                        jo_string (j, "action", "Written");
                        jo_string (j, "filename", filename);
                        jo_int (j, "bytes", writebytes);
                        jo_int (j, "ms", writetime / 1000ULL);
                        if (b.overrun)
                           jo_bool (j, "overrun", 1);
                        revk_info ("SD", &j);
                     }
                     if (b.overrun)
                     {          // Name
                        char *new = strdup (filename);
                        char *dot = strstr (new, ".WAV");
                        if (dot)
                        {
                           strcpy (dot, ".BAD");
                           rename (filename, new);
                        }
                        free (new);
                     }
                     free (filename);
                     filename = NULL;
                  }
               }
            }
            if (sdfile && sdin != sdout)
            {
               while (sdin != sdout)
               {
                  uint64_t a = esp_timer_get_time ();
                  fwrite (micaudio[sdout], micsamples * micchannels * micbytes, 1, sdfile);
                  writetime += esp_timer_get_time () - a;
                  writebytes += micsamples * micchannels * micbytes;
                  sdout = (sdout + 1) % MICQUEUE;
               }
            }
            usleep (10000);
         }
         free (filename);
         filename = NULL;
         rgbsd = 'B';
         // All done, unmount partition and disable SPI peripheral
         esp_vfs_fat_sdcard_unmount (sd_mount, card);
         ESP_LOGE (TAG, "SD Card dismounted");
         {
            jo_t j = jo_object_alloc ();
            jo_string (j, "action", cardstatus = "Dismounted");
            revk_info ("SD", &j);
         }
      }
   }
   revk_gpio_set (sddat3, 1);
   rtc_gpio_set_direction_in_sleep (sddat3.num, RTC_GPIO_MODE_OUTPUT_ONLY);
   rtc_gpio_set_level (sddat3.num, 1 - sddat3.invert);
   vTaskDelete (NULL);
}

rmt_symbol_word_t rmt_rx_symbols[64];
rmt_rx_done_event_data_t rx_data;

static bool
rmt_rx_done_callback (rmt_channel_handle_t channel, const rmt_rx_done_event_data_t * edata, void *user_data)
{
   BaseType_t high_task_wakeup = pdFALSE;
   QueueHandle_t receive_queue = (QueueHandle_t) user_data;
   xQueueSendFromISR (receive_queue, edata, &high_task_wakeup);
   return high_task_wakeup == pdTRUE;
}

void
ir_task (void *arg)
{
   revk_gpio_input (ir);
   rmt_rx_channel_config_t rx_channel_cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 1000000,
      .mem_block_symbols = sizeof (rmt_rx_symbols) / sizeof (*rmt_rx_symbols),
      .gpio_num = ir.num,
      .flags.invert_in = ir.invert,
#ifdef	CONFIG_IDF_TARGET_ESP32S3
      .flags.with_dma = 1,
#endif
   };
   rmt_channel_handle_t rx_channel = NULL;
   REVK_ERR_CHECK (rmt_new_rx_channel (&rx_channel_cfg, &rx_channel));
   if (!rx_channel)
   {
      ESP_LOGE (TAG, "No RMT Rx");
      vTaskDelete (NULL);
      return;
   }
   QueueHandle_t receive_queue = xQueueCreate (1, sizeof (rmt_rx_done_event_data_t));
   if (!receive_queue)
   {
      ESP_LOGE (TAG, "No RMT Queue");
      vTaskDelete (NULL);
      return;
   }
   rmt_rx_event_callbacks_t cbs = {
      .on_recv_done = rmt_rx_done_callback,
   };
   REVK_ERR_CHECK (rmt_rx_register_event_callbacks (rx_channel, &cbs, receive_queue));

   rmt_receive_config_t receive_config = {
      .signal_range_min_ns = 1250,      // the shortest duration for NEC signal is 560us, 1250ns < 560us, valid signal won't be treated as noise
      .signal_range_max_ns = 12000000,  // the longest duration for NEC signal is 9000us, 12000000ns > 9000us, the receive won't stop early
   };

   REVK_ERR_CHECK (rmt_enable (rx_channel));
   REVK_ERR_CHECK (rmt_receive (rx_channel, rmt_rx_symbols, sizeof (rmt_rx_symbols), &receive_config));

   while (1)
   {
      if (xQueueReceive (receive_queue, &rx_data, pdMS_TO_TICKS (1000)) == pdPASS)
      {
         ESP_LOGE (TAG, "Symbols %d", rx_data.num_symbols);
         // TODO decode

         // Next
         REVK_ERR_CHECK (rmt_receive (rx_channel, rmt_rx_symbols, sizeof (rmt_rx_symbols), &receive_config));
      }
   }
   vTaskDelete (NULL);
}

void
do_upload (void)
{
   while (1)
   {
      DIR *dir = opendir (sd_mount);
      if (!dir)
         break;
      struct dirent *entry;
      char *oldest = NULL;
      char *filename = NULL;
      while ((entry = readdir (dir)))
         if (entry->d_type == DT_REG)
         {
            if (!isdigit ((int) *(unsigned char *) (entry->d_name)))
               continue;
            const char *e = strrchr (entry->d_name, '.');
            if (!e || strcasecmp (e, ".wav"))
               continue;
            for (e = entry->d_name; *e && isdigit ((int) *(unsigned char *) e); e++);
            if (*e != '-')
               continue;
            if (!oldest || atoi (entry->d_name) < atoi (oldest))
            {
               free (oldest);
               oldest = strdup (entry->d_name);
            }
         }
      if (oldest)
      {
         asprintf (&filename, "%s/%s", sd_mount, oldest);
         free (oldest);
      }
      closedir (dir);
      if (!oldest)
         break;
      struct stat s = { 0 };
      if (stat (filename, &s))
      {
         free (filename);
         break;
      }
      if (!s.st_size)
      {
         ESP_LOGI (TAG, "Empty file %s", filename);
         unlink (filename);
         free (filename);
         continue;
      }
      ESP_LOGI (TAG, "Send %s", filename);
      int response = 0;
      FILE *i = fopen (filename, "r");
      if (!i)
      {
         ESP_LOGI (TAG, "Cannot open %s", filename);
         free (filename);
         break;
      }
      ESP_LOGE (TAG, "Upload %s, %lu bytes", filename, s.st_size);
      char *u;
      asprintf (&u, "%s?%s-%s", sdupload, hostname, filename + sizeof (sd_mount));
      for (char *p = u + strlen (sdupload) + 1; *p; p++)
         if (!is_alnum (*p) && *p != '.')
            *p = '-';
      esp_http_client_config_t config = {
         .url = u,
         .crt_bundle_attach = esp_crt_bundle_attach,
         .method = HTTP_METHOD_POST,
      };
#define	BLOCK	2048
      char *buf = mallocspi (BLOCK);
      if (buf)
      {
         esp_http_client_handle_t client = esp_http_client_init (&config);
         if (client)
         {
            esp_http_client_set_header (client, "Content-Type", "audio/wav");
            if (!esp_http_client_open (client, s.st_size))
            {                   // Send
               int total = 0;
               int len = 0;
               while ((len = fread (buf, 1, BLOCK, i)) > 0)
               {
                  total += len;
                  esp_http_client_write (client, buf, len);
               }
               esp_http_client_fetch_headers (client);
               esp_http_client_flush_response (client, &len);
               response = esp_http_client_get_status_code (client);
               esp_http_client_close (client);
            }
            esp_http_client_cleanup (client);
         }
      }
      free (u);
      free (buf);
#undef	BLOCK
      ESP_LOGI (TAG, "Sent, Response %d", response);
      if (response / 100 == 2)
      {
         char *new = strdup (filename);
         for (char *e = new; *e; e++)
            if (*e == '-')
            {
               *e = '_';
               break;
            }
         if (rename (filename, new))
         {
            free (filename);
            free (new);
            break;
         }
         ESP_LOGI (TAG, "Rename %s %s", filename, new);
         free (new);
      }
      fclose (i);
      free (filename);
      if (response / 100 != 2)
         break;
   }
}

void
mic_task (void *arg)
{
   led_strip_handle_t led_mic = NULL;
   if (rgbled.set)
   {
      led_strip_config_t strip_config = {
         .strip_gpio_num = rgbled.num,
         .max_leds = 2,
         .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
         .led_model = LED_MODEL_WS2812, // LED strip model
         .flags.invert_out = rgbled.invert,
      };
      led_strip_rmt_config_t rmt_config = {
         .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
         .resolution_hz = 10 * 1000 * 1000,     // 10 MHz
      };
      REVK_ERR_CHECK (led_strip_new_rmt_device (&strip_config, &rmt_config, &led_mic));
   }
   void led (char c)
   {
      if (led_mic)
      {
         revk_led (led_mic, 0, 255, micchannels == 0 || micchannels == 2 || !micright ? revk_rgb (c) : 0);
         revk_led (led_mic, 1, 255, micchannels == 0 || micchannels == 2 || micright ? revk_rgb (c) : 0);
         REVK_ERR_CHECK (led_strip_refresh (led_mic));
      }
   }
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
         led (rgbsd);
         usleep (100000);
         continue;
      }
      revk_disable_upgrade ();
      esp_err_t err;
      i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG (I2S_NUM_AUTO, I2S_ROLE_MASTER);
      if (b.sharedi2s)
         err = i2s_new_channel (&chan_cfg, &spk_handle, &mic_handle);   // Shared
      else
         err = i2s_new_channel (&chan_cfg, NULL, &mic_handle);
      uint8_t rawbytes = (micws.set ? micgain ? 4 : 3 : 2);     // No WS means PDM (16 bit)
      if (mode == MIC_SIP)
      {
         led ('C');
         micfreq = SIP_RATE;
         micchannels = 1;
         micbytes = 2;
         micsamples = SIP_BYTES;
      } else if (mode == MIC_RECORD)
      {
         micfreq = micrate;
         micchannels = (micstereo ? 2 : 1);
         micbytes = 2;
         micsamples = micfreq * MICMS / 1000;
         led (micbeep ? 'R' : 'G');
         if (wifirecord && !wifiusb && !b.usb)
            revk_disable_wifi ();
      }
      b.overrun = 0;
      sdin = sdout = 0;
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
      uint8_t beep = 0;
      uint8_t phase = 0;
      if (micbeep)
         beep = 1000 / MICMS + 1;
      ESP_LOGE (TAG, "Mic started mode %d, %ld*%d*%d bits at %ldHz - mapped to %d*%d bits", mode, micsamples, micchannels,
                rawbytes * 8, micfreq, micchannels, micbytes * 8);
      while (!b.die && !(sip_mode <= SIP_REGISTERED && !b.micon))
      {
         if (beep && !--beep)
            led ('G');
         size_t n = 0;
         i2s_channel_read (mic_handle, raw ? : micaudio[sdin], micchannels * rawbytes * micsamples, &n, MICMS * 2);
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
         switch (mode)
         {
         case MIC_SIP:
            if (sip_mode == SIP_IC || sip_mode == SIP_OG)
            {
               int16_t *i = (void *) micaudio[sdin];
               uint8_t *o = (void *) micaudio[sdin];
               for (int s = 0; s < micsamples; s++)
                  *o++ = sip_pcm13_to_rtp[(*i++) / 8 + 4096];
               sip_audio (micsamples, (void *) micaudio[sdin]);
            }
            break;
         case MIC_RECORD:      // These are not as fast as SIP, so do LED
            {
               if (beep)
               {
                  uint8_t *p = (void *) micaudio[sdin];
                  uint8_t f = ((micrate / 2000) / 2 ? : 1);
                  for (int s = 0; s < micsamples; s++)
                  {
                     for (int c = 0; c < micchannels; c++)
                     {
                        for (int z = 0; z < micbytes - 1; z++)
                           *p++ = 0;
                        *p++ = ((phase >= f) ? 0xF8 : 0x08);
                     }
                     phase++;
                     if (phase == f * 2)
                        phase = 0;
                  }
               }
               if ((sdin + 1) % MICQUEUE == sdout)
               {
                  if (!b.overrun)
                  {
                     b.overrun = 1;
                     led ('R');
                  }
                  ESP_LOGE (TAG, "Mic overflow");
               } else
                  sdin = (sdin + 1) % MICQUEUE;
            }
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
      revk_enable_upgrade ();
      if (wifirecord && !wifiusb && !b.usb)
         revk_enable_wifi ();
      ESP_LOGE (TAG, "Mic stopped");
   }
   led ('K');
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
      uint8_t morseprosign = 0;
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
      uint16_t on = 1,
         off = 0,
         morseu = 0,            // Timings for morse
         morsef = 0,
         dtmfu = 0,             // Timings for DTMF
         dtmfg = 0;
      uint32_t phase1 = 0,
         phase2 = 0,
         freq1 = 0,
         freq2 = 0;
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
         level1 = 50;           // Ring beeps
         freq1 = 1000;
         on = SIP_RATE / 3;
         off = SIP_RATE / 10;
      }
      if (mode == SPK_TONE)
      {
         if (tones)
         {
            tonep = tones;      // New tones
            dtmfu = dtmftone * spkfreq / 1000;
            dtmfg = dtmfgap * spkfreq / 1000;
         }
         if (morsemessage)
         {
            morsep = morsemessage;      // New message
            morsef = (60 * spkfreq / morsefwpm - 31 * morseu) / 19;
            if (morsef < morseu)
               morsef = morseu;
            morseu = 60 * spkfreq / morsewpm / 50;
         }
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
                     on--;
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
                        if (!morseprosign)
                           off = morsef * 3 - morseu;   // Character gap (we did morseu already)
                        while (*morsep == '[')
                        {
                           morseprosign = 1;
                           morsep++;
                        }
                        if (!*morsep)
                        {       // End of morse
                           morseprosign = 0;
                           morsep = NULL;
                           free (morsemessage);
                           morsemessage = NULL;
                           off = morsef * 7;    // Word gap
                           continue;
                        }
                        char c = toupper ((int) *morsep++);
                        while (*morsep == ']')
                        {
                           morseprosign = 0;
                           morsep++;
                        }
                        for (int i = 0; i < sizeof (morse) / sizeof (*morse); i++)
                           if (morse[i].c == c)
                           {
                              tonep = tones = strdup (morse[i].m);
                              break;
                           }
                        if (!tones)
                        {
                           off = morsef * 7 - morseu;   // Word gap
                           continue;
                        }
                        if (off)
                           continue;
                     }
                  }
                  if (!tones)
                  {             // End
                     if (morsep)
                        off = morsef * 7 - morseu;      // Word gap (we did morseu already)
                     else
                     {
                        mode = SPK_IDLE;
                        off = dtmfg;
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
               i2s_channel_write (spk_handle, samples, spkbytes * spkchannels * spksamples, &l, SPKMS * 2);
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
                  if (on)
                  {
                     on--;
                     samples[i] = tablesin (phase1) * (int) level1 / 100 / 2;
                     phase1 += freq1;
                     if (phase1 >= SIP_RATE)
                        phase1 -= SIP_RATE;
                     continue;
                  }
                  if (off)
                  {
                     off--;
                     samples[i] = 0;
                     if (!off)
                     {
                        freq1 = 3000 - freq1;
                        on = off = SIP_RATE / 10;
                     }
                  }
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
   rtc_gpio_set_level (spkpwr.num, sddat3.invert);
   vTaskDelete (NULL);
}

void
sip_debug (uint8_t rx, struct sockaddr_storage *addr, const char *message)
{
   ESP_LOG_BUFFER_HEX_LEVEL (rx ? "Rx" : "Tx", addr, sizeof (*addr), ESP_LOG_ERROR);
   ESP_LOGE (rx ? "Rx" : "Tx", "%s", message);
   // TODO log to MQTT
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
         if ((spk_mode && spk_mode != SPK_SIP) || (mic_mode && mic_mode != MIC_SIP))
         {
            ESP_LOGE (TAG, "Busy %d/%d", spk_mode, mic_mode);
            sip_hangup ();
         } else if (!button.set || !spklrc.set)
         {
            ESP_LOGE (TAG, "Answer");
            sip_answer ();
         }
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
      register_post_uri ("/", web_root);
   }
   led_strip_handle_t led_status = NULL;
   if (rgbstatus.set)
   {
      led_strip_config_t strip_config = {
         .strip_gpio_num = rgbstatus.num,
         .max_leds = 2,
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
   // Tasks
   if (spklrc.set && spkbclk.set && spkdata.set)
      revk_task ("spk", spk_task, NULL, 8);
   if (micdata.set && micclock.set)
      revk_task ("mic", mic_task, NULL, 8);
   if (sdcmd.set && sddat0.set && sdclk.set)
      revk_task ("sd", sd_task, NULL, 16);
   if (ir.set)
      revk_task ("ir", ir_task, NULL, 4);

   if (*siphost)
      sip_register (siphost, sipuser, sippass, sip_callback, sipdebug ? sip_debug : NULL);

   // Buttons and LEDs
   revk_gpio_input (button);
   revk_gpio_input (charging);  // On, off, or flashing
   revk_gpio_input (vbus);      // USB status
   uint8_t press = 255;
   uint8_t charge = 0;
   uint8_t usb = 1;
   uint32_t idle = 0;
   while (!b.die)
   {
      usleep (100000);
      uint32_t up = uptime ();
      if (autooff && !spk_mode && !mic_mode && (!usb || !autousb))
      {                         // Idle
         if (!idle)
            idle = up + autooff;
         else if (idle < up)
            b.die = 1;
      } else
         idle = 0;
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
      if (revk_shutting_down (NULL) && b.micon)
         b.micon = 0;
      if (charging.set)
         charge = (charge << 1) | revk_gpio_get (charging);
      revk_blink (0, 0, b.micon ? "K" : !usb ? "C" : charge == 0xFF ? "Y" : !charge ? "R" : "G");
      if (revk_gpio_get (button))
      {                         // Pressed
         if (press < 255)
            press++;
         if (press == 10 && sip_mode == SIP_IC_ALERT)
            sip_hangup ();
         if (press == 30)
            b.die = 1;
      } else if (press)
      {                         // Released
         if (press < 30)
         {
            if (sip_mode == SIP_IC_ALERT)
               sip_answer ();
            else if (sip_mode == SIP_IC || sip_mode == SIP_OG || sip_mode == SIP_OG_ALERT)
               sip_hangup ();
            else if (press >= 10 && *sipoutgoing)
               sip_call (NULL, sipoutgoing, strchr (sipoutgoing, '@') ? NULL : siphost, sipuser, sippass);
            else if (!b.micon && !b.sdpresent)
               ESP_LOGE (TAG, "No card");
            else
               b.micon = 1 - b.micon;
         }
         press = 0;
      }
      if (led_status)
      {
         uint32_t c = revk_blinker ();
         revk_led (led_status, 0, 255, c);
         revk_led (led_status, 1, 255, c);      // TODO something more useful??
         REVK_ERR_CHECK (led_strip_refresh (led_status));
      }
   }
   if (*sdupload && b.sdpresent)
   {                            // Upload
      revk_led (led_status, 0, 255, revk_rgb ('B'));
      revk_led (led_status, 1, 255, revk_rgb ('K'));
      REVK_ERR_CHECK (led_strip_refresh (led_status));
      revk_enable_wifi ();
      revk_wait_wifi (10);
      revk_led (led_status, 1, 255, revk_rgb ('R'));
      REVK_ERR_CHECK (led_strip_refresh (led_status));
      do_upload ();
   }
   // Go dark
   if (led_status)
   {
      revk_led (led_status, 0, 255, 0);
      revk_led (led_status, 1, 255, 0);
      REVK_ERR_CHECK (led_strip_refresh (led_status));
   }
   revk_pre_shutdown ();
   // Alarm
   if (rtc_gpio_is_valid_gpio (button.num))
   {                            // Deep sleep
      rtc_gpio_set_direction_in_sleep (button.num, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en (button.num);
      rtc_gpio_pulldown_dis (button.num);
      REVK_ERR_CHECK (esp_sleep_enable_ext0_wakeup (button.num, 1 - button.invert));
   } else
   {                            // Light sleep
      gpio_wakeup_enable (button.num, GPIO_INTR_LOW_LEVEL);
      esp_sleep_enable_gpio_wakeup ();
   }
   // Shutdown
   sleep (1);                   // Allow tasks to end
   // Night night
   if (rtc_gpio_is_valid_gpio (button.num))
      esp_deep_sleep_start ();
   else
      esp_light_sleep_start ();
   esp_restart ();
}
