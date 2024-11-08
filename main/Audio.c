/* Audio app */
/* Copyright ©2019 - 23 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static const char TAG[] = "Audio";

#include "revk.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/i2c.h>
#include <driver/i2s_pdm.h>
#include <driver/i2s_std.h>
#include <esp_http_server.h>
#include "fft.h"
#include "math.h"

#define AUDIOOVERSAMPLE 4       // From raw to FFT
#define AUDIOHZ         ((int)audiorate)      // Hz step
#define AUDIOSAMPLES    512     // Power of 2 (this is multiplied by oversample)
#define AUDIORATE       (AUDIOSAMPLES*AUDIOHZ)  // Hz which is multiplied by oversample (TDK 25-300ks/s in theory but 25k seemed not to work)
#define AUDIOMIN        ((int)audiorate*2)    // Hz
#define AUDIOMAX        ((int)audiorate*(AUDIOSAMPLES/2))     // Hz
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
   if (revk_link_down () && webcontrol >= 2)
      return revk_web_settings (req);   // Direct to web set up
   revk_web_head (req, "Audio");
   return revk_web_foot (req, 0, webcontrol >= 2 ? 1 : 0, NULL);
}

float audiomag = 0;
float audioband[AUDIOBANDS] = { 0 };

SemaphoreHandle_t audio_mutex = NULL;
void
i2s_task (void *arg)
{
   ESP_LOGE (TAG, "I2S init CLK %d DAT %d", audioclock.num, audiodata.num);
   jo_t e (esp_err_t err, const char *msg)
   {                            // Error
      jo_t j = jo_object_alloc ();
      if (msg)
         jo_string (j, "message", msg);
      if (err)
         jo_string (j, "error", esp_err_to_name (err));
      if (audiodata.set)
         jo_int (j, "data", audiodata.num);
      if (audioclock.set)
         jo_int (j, "clock", audioclock.num);
      return j;
   }
   esp_err_t err;
   i2s_chan_handle_t i = { 0 };
   i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG (I2S_NUM_AUTO, I2S_ROLE_MASTER);
   err = i2s_new_channel (&chan_cfg, NULL, &i);
   uint8_t bytes = (audiows.set ? 4 : 2);
   uint8_t *audioraw = mallocspi (bytes * AUDIOSAMPLES * AUDIOOVERSAMPLE);
   float *fftre = mallocspi (sizeof (float) * AUDIOSAMPLES);
   float *fftim = mallocspi (sizeof (float) * AUDIOSAMPLES);
   float audiogain = AUDIOGAINMAX;
   if (audiows.set)
   {                            // 24 bit Philips format
      i2s_std_config_t cfg = {
         .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG (AUDIORATE * AUDIOOVERSAMPLE),
         .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG (I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
         .gpio_cfg = {
                      .mclk = I2S_GPIO_UNUSED,
                      .bclk = audioclock.num,
                      .ws = audiows.num,
                      .dout = I2S_GPIO_UNUSED,
                      .din = audiodata.num,
                      .invert_flags = {
                                       .mclk_inv = false,
                                       .bclk_inv = audioclock.invert,
                                       .ws_inv = audiows.invert,
                                       },
                      },
      };
      cfg.slot_cfg.slot_mask = (audioright ? I2S_STD_SLOT_RIGHT : I2S_STD_SLOT_LEFT);
      if (bytes == 3)
         cfg.clk_cfg.mclk_multiple = 384;
      if (!err)
         err = i2s_channel_init_std_mode (i, &cfg);
   } else
   {                            // PDM 16 bit
      i2s_pdm_rx_config_t cfg = {
         .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG (AUDIORATE * AUDIOOVERSAMPLE),
         .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG (I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
         .gpio_cfg = {
                      .clk = audioclock.num,
                      .din = audiodata.num,
                      .invert_flags = {
                                       .clk_inv = audioclock.invert}
                      }
      };
      cfg.slot_cfg.slot_mask = (audioright ? I2S_PDM_SLOT_RIGHT : I2S_PDM_SLOT_LEFT);
      if (!err)
         err = i2s_channel_init_pdm_rx_mode (i, &cfg);
   }
   gpio_pulldown_en (audiodata.num);
   if (!err)
      err = i2s_channel_enable (i);
   if (err)
   {
      ESP_LOGE (TAG, "I2S failed");
      jo_t j = e (err, "Failed init I2S");
      revk_error ("i2s", &j);
      vTaskDelete (NULL);
      return;
   }
   ESP_LOGE (TAG, "Audio started, %d*%d bits at %dHz", AUDIOSAMPLES * AUDIOOVERSAMPLE, bytes * 8, AUDIORATE * AUDIOOVERSAMPLE);
   while (1)
   {
      size_t n = 0;
      i2s_channel_read (i, audioraw, bytes * AUDIOSAMPLES * AUDIOOVERSAMPLE, &n, 100);
      if (n < bytes * AUDIOSAMPLES * AUDIOOVERSAMPLE)
         continue;
      if (*(int32_t *) audioraw)
         b.soundok = 1;
      if (!b.sound)
         continue;              // Not needed
      float ref = 0,
         mag = 0;
      {
         uint8_t *p = audioraw;
         for (int i = 0; i < AUDIOSAMPLES; i++)
         {
            int32_t raw;
            if (bytes == 4)
               raw = *(int32_t *) p << 1;       // Philips mode 24 bits with top bit skipped
            else
               raw = *(int16_t *) p << 16;      // PDM 16 bit mode
            p += bytes;
            float v = (float) raw / 2147483648;
            mag += v * v;
            fftre[i] = v * audiogain / AUDIOOVERSAMPLE;
            for (int q = 0; q < AUDIOOVERSAMPLE - 1; q++)
            {
               if (bytes == 4)
                  raw = *(int32_t *) p << 1;    // Philips mode
               else
                  raw = *(int16_t *) p << 16;   // PDM 16 bit mode
               p += bytes;
               float v = (float) raw / 2147483648;
               fftre[i] += v * audiogain / AUDIOOVERSAMPLE;
               mag += v * v;
            }
            fftim[i] = 0;
            ref += fftre[i] * fftre[i];
         }
      }
      // Gain adjust
      ref = sqrt (ref / AUDIOSAMPLES);
      if (ref > 1)
         audiogain = (audiogain * 9 + audiogain / ref) / 10;    // Drop gain faster if overloading
      else
         audiogain = (audiogain * 99 + audiogain / ref) / 100;  // Bring back gain slowly
      if (audiogain > AUDIOGAINMAX)
         audiogain = AUDIOGAINMAX;
      else if (audiogain < AUDIOGAINMIN)
         audiogain = AUDIOGAINMIN;
      fft (fftre, fftim, AUDIOSAMPLES);
      float band[AUDIOBANDS];   // Should get main audio in first 16 or so slots
      for (int b = 0; b < AUDIOBANDS; b++)
         band[b] = NAN;
      {                         // log frequency
         float low = log (AUDIOMIN),
            high = log (AUDIOMAX),
            step = (high - low) / AUDIOBANDS;
         for (int i = AUDIOMIN * AUDIOSAMPLES / AUDIORATE; i < AUDIOMAX * AUDIOSAMPLES / AUDIORATE && i < AUDIOSAMPLES / 2; i++)
         {
            float l = log (i * AUDIORATE / AUDIOSAMPLES);
            int b = (l - low) / step;
            if (b >= 0 && b < AUDIOBANDS)       // In case of rounding going too far!
            {
               fftre[i] /= (AUDIOSAMPLES / 2);
               fftim[i] /= (AUDIOSAMPLES / 2);
               float v = sqrt (fftre[i] * fftre[i] + fftim[i] * fftim[i]);
               if (isnan (band[b]))
                  band[b] = v;
               else
                  band[b] += v;
            }
         }
      }
      for (int b = 0; b < AUDIOBANDS - 1; b++)
         if (!isnan (band[b]) && isnan (band[b + 1]))
         {
            int q = 2;
            while (b + q < AUDIOBANDS && isnan (band[b + q]))
               q++;
            float v = band[b];
            for (int z = 0; z < q; z++)
               band[b + z] = v / q;
         }
      for (int b = 0; b < AUDIOBANDS; b++)
         band[b] = 10 * log10 (band[b]);        // OK no clue why but if we average we end up with way lower top frequencies
      //ESP_LOGE (TAG, "FFT %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f gain %6.2f", band[0], band[3], band[6], band[9], band[12], band[15], band[18], band[21], audiogain);
      for (int b = 0; b < AUDIOBANDS; b++)
         band[b] = (band[b] + 25) / 25; // makes more 0-1 level output
      //ESP_LOGE (TAG, "FFT %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f", band[0], band[3], band[6], band[9], band[12], band[15], band[18], band[21]);
      xSemaphoreTake (audio_mutex, portMAX_DELAY);
      audiomag = sqrt (mag / AUDIOSAMPLES / AUDIOOVERSAMPLE);
      for (int i = 0; i < AUDIOBANDS; i++)
      {
         if (band[i] > audioband[i] || !audiodamp)
            audioband[i] = band[i];
         else
            audioband[i] = (audioband[i] * audiodamp + band[i]) / (audiodamp + 1);
      }
      xSemaphoreGive (audio_mutex);
   }
   vTaskDelete (NULL);
}

uint8_t
audiohz2band (uint32_t hz)
{
   if (hz)
      return 0;
   float low = log (AUDIOMIN),
      high = log (AUDIOMAX),
      val = log (hz);
   int b = AUDIOBANDS * (val - low) / (high - low);
   if (b < 0)
      b = 0;
   if (b >= AUDIOBANDS)
      b = AUDIOBANDS;
   return b;
}

uint32_t
audioband2hz (uint8_t b)
{
   float low = log (AUDIOMIN),
      high = log (AUDIOMAX),
      val = b * (high - low) / AUDIOBANDS + low;
   return exp (val);
}

void
app_main ()
{
   audio_mutex = xSemaphoreCreateBinary ();
   xSemaphoreGive (audio_mutex);
   revk_boot (&app_callback);
   revk_start ();
   if (dark)
      revk_blink (0, 0, "K");

   if (audiodata.set && audioclock.set)
      revk_task ("i2s", i2s_task, NULL, 8);

   if (webcontrol)
   {                            // Web interface
      httpd_config_t config = HTTPD_DEFAULT_CONFIG ();  // When updating the code below, make sure this is enough
      //  Note that we 're also 4 adding revk' s web config handlers
      config.max_uri_handlers = 8;
      if (!httpd_start (&webserver, &config))
      {
         if (webcontrol >= 2)
            revk_web_settings_add (webserver);
         register_get_uri ("/", web_root);
      }
   }
}
