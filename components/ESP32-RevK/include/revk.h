// Include file for revk.c

#ifndef	REVK_H
#define	REVK_H

#include "sdkconfig.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#ifdef	CONFIG_REVK_MQTT
#include "lwmqtt.h"
#endif
#include "esp_http_server.h"
#include "jo.h"

#include "esp8266_httpd_compat.h"
#include "esp8266_netif_compat.h"
#ifdef  CONFIG_REVK_LED_STRIP
#include "led_strip.h"
#endif

#ifndef  CONFIG_REVK_OLD_SETTINGS
#include "../../settings.h"
#endif

#include "revk_ctype.h"

#ifndef CONFIG_MQTT_BUFFER_SIZE
#define CONFIG_MQTT_BUFFER_SIZE 2048
#endif
#ifdef  CONFIG_REVK_MESH
#define MQTT_MAX MESH_MPS
#else
#define MQTT_MAX CONFIG_MQTT_BUFFER_SIZE
#endif

// Types

        // MQTT rx callback: Do not consume jo_t! Return error or NULL. Returning "" means handled the command with no error.
        // You will want to check prefix matches prefixcommand
        // Target can be something not for us if extra subscribes done, but if it is for us, or internal, it is passes as NULL
        // Suffix can be NULL
typedef const char *app_callback_t (int client, const char *prefix, const char *target, const char *suffix, jo_t);
typedef uint8_t mac_t[6];

// Data
extern const char *revk_app;    // App name
extern const char *revk_version;        // App version
extern const char revk_build_suffix[];  // App build suffix
extern char revk_id[13];        // Chip ID hex (from MAC)
extern mac_t revk_mac;          // Our mac
extern uint64_t revk_binid;     // Chip ID binary

#ifdef  CONFIG_REVK_OLD_SETTINGS
typedef struct
{                               // Dynamic binary data
   uint16_t len;
   uint8_t data[];
} revk_bindata_t;

extern char *prefixcommand;
extern char *prefixsetting;
extern char *prefixstate;
extern char *prefixevent;
extern char *prefixinfo;
extern char *prefixerror;
extern char *appname;
extern char *hostname;
extern char *nodename;          // Node name
#endif

extern esp_netif_t *sta_netif;
extern esp_netif_t *ap_netif;
#ifdef  CONFIG_REVK_LED_STRIP
extern led_strip_handle_t revk_strip;
#endif

jo_t jo_make (const char *nodename);    // Start object with node name

#define freez(x) do{if(x){free((void*)x);x=NULL;}}while(0)      // Just useful - yes free(x) is valid when x is NULL, but this sets x NULL as a result as well

void *mallocspi (size_t);       // Malloc from SPI preferred
uint32_t uptime (void);         // Seconds uptime

// Calls
#ifdef  CONFIG_REVK_OLD_SETTINGS
#define	REVK_SETTINGS_HAS_GPIO  // Fixed definition in old settings
typedef struct revk_gpio_s revk_gpio_t;
struct revk_gpio_s
{
   uint8_t num:6;
   uint8_t invert:1;
   uint8_t set:1;
};
#endif
int gpio_ok (uint8_t gpio);     // non 0 if OK to use in current platform (bit 0 for out, bit 1 for in, bit 2 for special use - e.g. USB)
#ifdef	REVK_SETTINGS_HAS_GPIO
esp_err_t revk_gpio_output (revk_gpio_t g, uint8_t o);
esp_err_t revk_gpio_set (revk_gpio_t g, uint8_t o);
esp_err_t revk_gpio_input (revk_gpio_t g);
uint8_t revk_gpio_get (revk_gpio_t g);
#endif
void revk_boot (app_callback_t * app_callback);
void revk_start (void);
void revk_pre_shutdown (void);

#ifdef	CONFIG_REVK_OLD_SETTINGS
// Register a setting, call from init (i.e. this is not expecting to be thread safe) - sets the value when called and on revk_setting/MQTT changes
// Note, a setting that is SECRET that is a root name followed by sub names creates parent/child. Only shown if parent has value or default value (usually overlap a key child)
void revk_register (const char *name,   // Setting name (note max 15 characters inc any number suffix)
                    uint8_t array,      // If non zero then settings are suffixed numerically 1 to array
                    uint16_t size,      // Base setting size, -8/-4/-2/-1 signed, 1/2/4/8 unsigned, 0=null terminated string.
                    void *data, // The setting itself (for string this points to a char* pointer)
                    const char *defval, // default value (default value text, or bitmask[space]default)
                    int flags); // Setting flags
#define	SETTING_LIVE		1       // Setting update live (else reboots shortly after any change)
#define	SETTING_BINDATA		2       // Binary block (text is base64 or hex) rather than numeric. Fixed is just the data (malloc), variable is pointer to revk_bin_t
#define	SETTING_SIGNED		4       // Numeric is signed
#define	SETTING_BOOLEAN		8       // Boolean value (array sets bits)
#define	SETTING_BITFIELD	16      // Numeric value has bit field prefix (from defval string)
#define	SETTING_HEX		32      // Source string is hex coded
#define	SETTING_SET		64      // Set top bit of numeric if a value is present at all
#define	SETTING_SECRET		128     // Don't dump setting
#define	SETTING_FIX		256     // Store in flash regardless, so default only used on initial s/w run
#endif

#if CONFIG_LOG_DEFAULT_LEVEL > 2
esp_err_t revk_err_check (esp_err_t, const char *file, int line, const char *func, const char *cmd);    // Log if error
#define	REVK_ERR_CHECK(x) revk_err_check(x,__FILE__,__LINE__,__FUNCTION__,#x)
#else
esp_err_t revk_err_check (esp_err_t e);
#define	REVK_ERR_CHECK(x) revk_err_check(x)
#endif

// Make a task
TaskHandle_t revk_task (const char *tag, TaskFunction_t t, const void *param, int kstack);

// reporting via main MQTT, copy option is how many additional MQTT to copy, normally 0 or 1. Setting -N means send only to specific additional MQTT, return NULL for no error
const char *revk_mqtt_send_raw (const char *topic, int retain, const char *payload, uint8_t clients);
const char *revk_mqtt_send_payload_clients (const char *prefix, int retain, const char *suffix, const char *payload,
                                            uint8_t clients);
const char *revk_mqtt_send_str_clients (const char *str, int retain, uint8_t clients);
#define	revk_mqtt_send_str(s) revk_mqtt_send_str_clients(s,0,1)
const char *revk_state_clients (const char *suffix, jo_t *, uint8_t clients);
#define revk_state(t,j) revk_state_clients(t,j,1)
const char *revk_event_clients (const char *suffix, jo_t *, uint8_t clients);
#define revk_event(t,j) revk_event_clients(t,j,1)
const char *revk_error_clients (const char *suffix, jo_t *, uint8_t clients);
#define revk_error(t,j) revk_error_clients(t,j,1)
const char *revk_info_clients (const char *suffix, jo_t *, uint8_t clients);
#define revk_info(t,j) revk_info_clients(t,j,1)

const char *revk_mqtt_send_clients (const char *prefix, int retain, const char *suffix, jo_t * jp, uint8_t clients);
#define revk_mqtt_send(p,r,t,j) revk_mqtt_send_clients(p,r,t,j,1)

#define	REVK_SETTINGS_PASSOVERRIDE	1	// Ignore password
#define	REVK_SETTINGS_JSON_STRING	2	// Expect JSON fields to be strings
const char *revk_settings_store (jo_t, const char **, uint8_t flags);       // Store settings, return error (set location in j of error, valid while j valid), and error of "" is a non error but means some settings were changed, NULL is no change
#define	revk_setting(j) revk_settings_store(j,NULL,0)
const char *revk_command (const char *tag, jo_t);       // Do an internal command
const char *revk_restart (int delay, const char *fmt, ...);     // Restart cleanly
const char *revk_ota (const char *host, const char *target);    // OTA and restart cleanly (target NULL for self as root node)
uint32_t revk_shutting_down (const char **);    // If we are shutting down (how many seconds to go) - sets reason if not null
const char *revk_build_date (char d[20]);       // Get build date ISO formatted
int8_t revk_ota_progress (void);        // Progress (-2=up to date, -1=not, 0-100 is progress, 101=done)

#ifdef	CONFIG_REVK_MQTT
void revk_mqtt_init (void);
lwmqtt_t revk_mqtt (int);
void revk_mqtt_close (const char *reason);      // Clean close MQTT
int revk_wait_mqtt (int seconds);
#endif
#if	defined(CONFIG_REVK_WIFI) || defined(CONFIG_REVK_MESH)
uint32_t revk_link_down (void); // How long link down (no IP or no parent if mesh)
#define MESH_PAD        32      // Max extra allocated bytes required on data
const char *revk_wifi (void);   // Return the wifi SSID
void revk_wifi_close (void);    // Close wifi
int revk_wait_wifi (int seconds);       // Wait for wifi
#endif
#ifdef	CONFIG_REVK_MESH
extern uint16_t meshmax;
void revk_mesh_send_json (const mac_t mac, jo_t * jp);
#endif
#ifdef	CONFIG_REVK_MQTT
char *revk_topic (const char *name, const char *id, const char *suffix);
void revk_send_subunsub (int client, const mac_t,uint8_t sub);
#define revk_send_sub(c,m) revk_send_subunsub(c,m,1)
#define revk_send_unsub(c,m) revk_send_subunsub(c,m,0)
#endif
void revk_blink (uint8_t on, uint8_t off, const char *colours); // Set LED blink rate and colour sequence for on state (for RGB LED)
void revk_blink_init(void);	// Start library blinker
void revk_blink_do(void);	// Do library blinker (10Hz expected)

uint16_t revk_num_web_handlers (void);  // Number of handlers used by revk_web_settings_add()
const char *revk_web_safe (char **temp, const char *value);     // Return safe version of text for HTML (malloced in *temp)
void revk_web_send (httpd_req_t * req, const char *format, ...);
jo_t revk_web_query (httpd_req_t * req);        // Get post/get form in JSON form
esp_err_t revk_web_settings_add (httpd_handle_t webserver);     // Add URLs
esp_err_t revk_web_settings_remove (httpd_handle_t webserver);  // Remove URLs
esp_err_t revk_web_settings (httpd_req_t * req);        // Call for web config for SSID/password/mqtt (GET/POST) - needs 4 URLS
void revk_web_setting (httpd_req_t * req, const char *tag, const char *field);
esp_err_t revk_web_status (httpd_req_t * req);  // Call for web config for SSID/password/mqtt (WS)
esp_err_t revk_web_wifilist (httpd_req_t * req);        // WS for list of SSIDs
void revk_web_head (httpd_req_t * req, const char *title);      // Generic html heading
esp_err_t revk_web_foot (httpd_req_t * req, uint8_t home, uint8_t wifi, const char *extra);     // Generic html footing and return

uint32_t revk_blinker (void);   // Return colour for blinking LED (as per revk_rgb) plug top bit for basic blink cycle
#ifdef  CONFIG_REVK_LED_STRIP
extern const uint8_t gamma8[256];
uint32_t revk_rgb (char c);     // Provide RGB colour for character, with scaling, and so on, in bottom 3 bytes. Top byte has 2 bits per colour.
void revk_led (led_strip_handle_t strip, int led, uint8_t scale, uint32_t rgb); // Set LED from RGB with scale and apply gamma
#endif

#ifdef	CONFIG_REVK_SEASON
const char *revk_season (time_t now);   // Return a character for seasonal variation, E=Easter, Y=NewYear, X=Christmas, H=Halloween
#endif

#ifdef	CONFIG_REVK_LUNAR
time_t revk_moon_full_last (time_t t);  // last full moon (so <=t)
time_t revk_moon_new (time_t t);        // Current new moon - may be >t or <=t
time_t revk_moon_full_next (time_t t);  // next full moon (so >t)
#endif

#ifdef	CONFIG_REVK_SOLAR
time_t sun_find_crossing (time_t start_time, double latitude, double longitude, double wanted_altitude);
void sun_position (double t, double latitude, double longitude, double *altitude, double *azimuth);
time_t sun_rise (int y, int m, int d, double latitude, double longitude, double sun_altitude);
time_t sun_set (int y, int m, int d, double latitude, double longitude, double sun_altitude);
#define SUN_SIZE                        (50.0/60.0)
#define SUN_DEFAULT	                (-SUN_SIZE)
#define SUN_CIVIL_TWILIGHT              (-6.0)
#define SUN_NAUTICAL_TWILIGHT           (-12.0)
#define SUN_ASTRONOMICAL_TWILIGHT       (-18.0)
#endif

void revk_enable_wifi (void);
void revk_disable_wifi (void);
void revk_enable_ap (void);
void revk_disable_ap (void);
void revk_enable_settings (void);
void revk_disable_settings (void);

#endif
