// Home Assistant support / config librrary
#ifdef	CONFIG_REVK_HALIB

 // Path: NULL is base, /suffix is after base, non / is absolute path

typedef struct
{
   const char *id;              // tag/id no spaces
   const char *name;            // name
   const char *type;            // type
   const char *unit;            // unit
   const char *stat;            // stat topic (default main status)
   const char *field;           // field name (default id)

   const char *icon;            // icon
   uint8_t delete:1;            // deleted entry
} ha_config_t;

// Sensor
#define ha_config_sensor(...)  ha_config_opts("sensor",(ha_config_t){__VA_ARGS__})
const char *ha_config_opts (const char *, ha_config_t);

#endif
