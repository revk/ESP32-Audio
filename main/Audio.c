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
#include <esp_http_server.h>
#include "fft.h"
#include "math.h"

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
} b;

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
      morsemessage = jo_strdup (j);
      return NULL;
   }
   if (!strcasecmp (suffix, "dtmf"))
   {
      if (dtmfmessage)
         return "Wait";
      if (jo_here (j) != JO_STRING)
         return "JSON string";
      dtmfmessage = jo_strdup (j);
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

SemaphoreHandle_t audio_mutex = NULL;

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
      ESP_LOGE (TAG, "I2S failed");
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
               off = unit2;     // intra character
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
   audio_mutex = xSemaphoreCreateBinary ();
   xSemaphoreGive (audio_mutex);
   revk_boot (&app_callback);
   revk_start ();

   if (spklrc.set && spkbclk.set && spkdata.set)
      revk_task ("spk", spk_task, NULL, 8);

   httpd_config_t config = HTTPD_DEFAULT_CONFIG ();     // When updating the code below, make sure this is enough
   //  Note that we 're also 4 adding revk' s web config handlers
   config.max_uri_handlers = 8;
   if (!httpd_start (&webserver, &config))
   {
      revk_web_settings_add (webserver);
      register_get_uri ("/", web_root);
   }
}
