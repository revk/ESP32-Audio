#ifndef REVK_ESP8266_NVS_COMPAT_H
#define REVK_ESP8266_NVS_COMPAT_H

#ifdef CONFIG_IDF_TARGET_ESP8266

static inline esp_err_t
nvs_entry_find_compat(const char *part_name, const char *namespace_name, nvs_type_t type, nvs_iterator_t *output_iterator)
{
    *output_iterator = nvs_entry_find(part_name, namespace_name, type);
    return *output_iterator ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

static inline esp_err_t
nvs_entry_info_compat(const nvs_iterator_t iterator, nvs_entry_info_t *out_info)
{
    nvs_entry_info(iterator, out_info);
    return ESP_OK;
}

static inline esp_err_t
nvs_entry_next_compat(nvs_iterator_t *iterator)
{
    *iterator = nvs_entry_next(*iterator);
    return *iterator ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

#else

static inline esp_err_t
nvs_entry_find_compat(const char *part_name, const char *namespace_name, nvs_type_t type, nvs_iterator_t *output_iterator)
{
    return nvs_entry_find(part_name, namespace_name, type, output_iterator);
}

static inline esp_err_t
nvs_entry_info_compat(const nvs_iterator_t iterator, nvs_entry_info_t *out_info)
{
    return nvs_entry_info(iterator, out_info);
}

static inline esp_err_t
nvs_entry_next_compat(nvs_iterator_t *iterator)
{
    return nvs_entry_next(iterator);
}

#endif
#endif
