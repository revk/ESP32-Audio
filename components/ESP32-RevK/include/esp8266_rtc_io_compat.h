#ifndef REVK_ESP8266_RTC_IO_COMPAT_H
#define REVK_ESP8266_RTC_IO_COMPAT_H

#ifdef CONFIG_IDF_TARGET_ESP8266

/*
 * These function are stubs. ESP8266-IDF drives pin no 16 (which is the only RTC GPIO)
 * using common GPIO functions transparently.
 */
static inline bool
rtc_gpio_is_valid_gpio(gpio_num_t gpio_num)
{
    return 0;
}

static inline esp_err_t
rtc_gpio_deinit(gpio_num_t gpio_num)
{
    return ESP_ERR_INVALID_ARG;
}

#define RTC_GPIO_MODE_OUTPUT_OD   GPIO_MODE_OUTPUT_OD
#define RTC_GPIO_MODE_OUTPUT_ONLY GPIO_MODE_OUTPUT

static inline esp_err_t
rtc_gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    return ESP_ERR_INVALID_ARG;
}

static inline esp_err_t
rtc_gpio_set_drive_capability(gpio_num_t gpio_num, int strength)
{
    return ESP_ERR_INVALID_ARG;
}

static inline esp_err_t
rtc_gpio_pullup_en (gpio_num_t gpio_num)
{
    return ESP_ERR_INVALID_ARG;
}

static inline esp_err_t
rtc_gpio_pullup_dis (gpio_num_t gpio_num)
{
    return ESP_ERR_INVALID_ARG;
}

static inline esp_err_t
rtc_gpio_pulldown_en (gpio_num_t gpio_num)
{
    return ESP_ERR_INVALID_ARG;
}

static inline esp_err_t
rtc_gpio_pulldown_dis (gpio_num_t gpio_num)
{
    return ESP_ERR_INVALID_ARG;
}

#else

#include <driver/rtc_io.h>

#endif
#endif
