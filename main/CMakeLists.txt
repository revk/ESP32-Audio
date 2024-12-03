set (COMPONENT_SRCS "OpenMic.c" "fft.c" "../settings.c")
set (COMPONENT_REQUIRES "ESP32-RevK" "ESP32-SIP" "fatfs" "sdmmc" "driver" "led_strip")


if("$ENV{SUFFIX}" MATCHES ".*-MT.*")

list(APPEND COMPONENT_REQUIRES "esp_matter")
list(APPEND COMPONENT_SRCS "matter_main.cpp")

set(PRIV_REQUIRES_LIST esp_matter esp_matter_console)

idf_component_register(
	SRCS ${COMPONENT_SRCS}
	REQUIRES ${COMPONENT_REQUIRES}
	SRC_DIRS          "."
	PRIV_INCLUDE_DIRS  "."
	PRIV_REQUIRES      ${PRIV_REQUIRES_LIST})

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 14)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

else()

register_component ()

endif()

