cmake_minimum_required(VERSION 3.5)

set(SOURCES "revk.c" "jo.c" "lwmqtt.c" "settings_lib.c" "settings_old.c" "halib.c")
set(RECS nvs_flash app_update esp_http_client esp-tls esp_http_server spi_flash esp_wifi esp_timer esp_system driver bt)

# Add extra dependancies

# We should be able to do if(CONFIG_IDF_TARGET_ESP8266), and the IDF documentation
# is quite clear that we should be able to do that, but it does not seem to work.

if(EXISTS $ENV{IDF_PATH}/components/esp8266)
	list(APPEND SOURCES "esp8266_compat.c")
	set(RECS nvs_flash app_update esp_http_client esp-tls esp_http_server esp8266 spi_flash)
endif()

if(EXISTS "../managed_components/espressif__mdns")
	list(APPEND RECS mdns)
endif()

if(EXISTS "../managed_components/espressif__led_strip")
	list(APPEND RECS led_strip)
endif()

idf_component_register(
			SRCS ${SOURCES}
			INCLUDE_DIRS "include"
			REQUIRES ${RECS}
			EMBED_FILES "../../build/partition_table/partition-table.bin"
)

add_definitions(-DBUILD_ESP32_USING_CMAKE)
set_property(SOURCE "../../build/partition_table/partition-table.bin" PROPERTY GENERATED 1)

