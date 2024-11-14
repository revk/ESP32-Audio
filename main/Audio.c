/* Audio app */
/* Copyright ©2019 - 23 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

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

#define AUDIOOVERSAMPLE 4       // From raw to FFT
#define AUDIOHZ         ((int)audiorate)        // Hz step
#define AUDIOSAMPLES    512     // Power of 2 (this is multiplied by oversample)
#define AUDIORATE       (AUDIOSAMPLES*AUDIOHZ)  // Hz which is multiplied by oversample (TDK 25-300ks/s in theory but 25k seemed not to work)
#define AUDIOMIN        ((int)audiorate*2)      // Hz
#define AUDIOMAX        ((int)audiorate*(AUDIOSAMPLES/2))       // Hz
#define AUDIOBANDS      42      // How many bands we make log based
#define AUDIOSTEP       ((AUDIOMAX-AUDIOMIN)/AUDIOBANDS)        // Hz steps
#define AUDIOGAINMIN    0.01
#define AUDIOGAINMAX    (audiomaxgain)

struct
{
   uint8_t sound:1;             // An audio based effect is in use
   uint8_t soundok:1;           // Receiving sound data
   uint8_t checksound:1;        // Temp
} b;

static httpd_handle_t webserver = NULL;

const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   if (client || !prefix || target || strcmp (prefix, topiccommand))
      return NULL;              // Not for us or not a command from main MQTTS

   return NULL;
}

void
revk_web_extra (httpd_req_t * req, int page)
{
   revk_web_setting (req, NULL, "dark");
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
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG (I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
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
   uint32_t f = 1000;
   int32_t *samples = mallocspi (sizeof (int32_t) * SAMPLES);
   uint32_t p = 0;
   while (1)
   {
      size_t l = 0;
      for (int i = 0; i < SAMPLES; i++)
      {
         samples[i] = 2147483647 * sin (M_PI * 2 * p / spkrate)/10;
         p += f;
         if (p >= spkrate)
            p -= spkrate;
      }
      i2s_channel_write (tx_handle, samples, sizeof (int32_t) * SAMPLES, &l, 100);
      ESP_LOGE (TAG, "Sent");
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
