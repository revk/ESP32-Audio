// (old) settings library
#include "revk.h"
#ifdef  CONFIG_REVK_OLD_SETTINGS
#ifdef  CONFIG_REVK_MESH
#include <esp_mesh.h>
#endif
static const char __attribute__((unused)) * TAG = "Settings";
extern char *topicsetting;

typedef struct setting_s setting_t;
struct setting_s
{
   nvs_handle nvs;              // Where stored
   setting_t *next;             // Next setting
   const char *name;            // Setting name
   const char *defval;          // Default value, or bitfield{[space]default}
   void *data;                  // Stored data
   uint16_t size;               // Size of data, 0=dynamic
   uint8_t array;               // array size
   uint8_t namelen;             // Length of name
   uint16_t flags:9;            // flags
   uint16_t set:1;              // Has been set
   uint16_t parent:1;           // Parent setting
   uint16_t child:1;            // Child setting
   uint16_t dup:1;              // Set in parent if it is a duplicate of a child
   uint16_t used:1;             // Used in settings as temp
};
static setting_t *setting = NULL;

extern nvs_handle revk_nvs;
extern uint32_t revk_nvs_time;
extern uint8_t prefixapp;

static int nvs_get (setting_t * s, const char *tag, void *data, size_t len);
static const char *revk_setting_internal (setting_t * s, unsigned int len, const unsigned char *value, unsigned char index,
                                          int flags);

void
revk_settings_load (const char *tag, const char *appname)
{                               // Not applicable in old library
}

void
revk_register (const char *name, uint8_t array, uint16_t size, void *data, const char *defval, int flags)
{                               /* Register setting (not expected to be thread safe, should be called from init) */
   ESP_LOGD (TAG, "Register %s", name);
   if (flags & SETTING_BITFIELD)
   {
      if (!defval)
         ESP_LOGE (TAG, "%s missing defval on bitfield", name);
      else if (!size)
         ESP_LOGE (TAG, "%s missing size on bitfield", name);
      else
      {
         const char *p = defval;
         while (*p && *p != ' ')
            p++;
         if ((p - defval) > 8 * size - ((flags & SETTING_SET) ? 1 : 0))
            ESP_LOGE (TAG, "%s too small for bitfield", name);
      }
   }
   setting_t *s;
   for (s = setting; s && strcmp (s->name, name); s = s->next);
   if (s)
      ESP_LOGE (TAG, "%s duplicate", name);
   s = mallocspi (sizeof (*s));
   memset (s, 0, sizeof (*s));
   s->nvs = revk_nvs;
   s->name = name;
   s->namelen = strlen (name);
   s->array = array;
   s->size = size;
   s->data = data;
   s->flags = flags;
   s->defval = defval;
   s->next = setting;
   {                            // Check if sub setting - parent must be set first, and be secret and same array size
      setting_t *q;
      for (q = setting;
           q && (q->namelen >= s->namelen || strncmp (q->name, name, q->namelen)
                 || !(q->flags & SETTING_SECRET) || q->array != array); q = q->next);
      if (q)
      {
         s->child = 1;
         q->parent = 1;
         if (s->data == q->data)
            q->dup = 1;
      }
   }
   setting = s;
   memset (data, 0, (size ? : sizeof (void *)) * (!(flags & SETTING_BOOLEAN) && array ? array : 1));    /* Initialise memory */
   /* Get value */
   int get_val (const char *tag, int index)
   {
      void *data = s->data;
      if (s->array && !(flags & SETTING_BOOLEAN))
         data += (s->size ? : sizeof (void *)) * index;
      int l = -1;
      if (!s->size)
      {                         /* Dynamic */
         void *d = NULL;
         l = nvs_get (s, tag, NULL, 0);
         if (l >= 0)
         {                      // Has data
            d = mallocspi (l);
            l = nvs_get (s, tag, d, l);
            *((void **) data) = d;
         } else
            l = -1;             /* default */
      } else
         l = nvs_get (s, tag, data, s->size);   /* Stored static */
      return l;
   }
   const char *e;
   if (array)
   {                            /* Work through tags */
      int i;
      for (i = 0; i < array; i++)
      {
         char tag[16];          /* NVS tag size */
         if (snprintf (tag, sizeof (tag), "%s%u", s->name, i + 1) < sizeof (tag) && get_val (tag, i) < 0)
         {
            e = revk_setting_internal (s, 0, NULL, i, SETTING_LIVE | (flags & SETTING_FIX));    /* Defaulting logic */
            if (e && *e)
               ESP_LOGE (TAG, "Setting %s failed %s", tag, e);
            else
               ESP_LOGD (TAG, "Setting %s created", tag);
         }
      }
   } else if (get_val (s->name, 0) < 0)
   {                            /* Simple setting, not array */
      e = revk_setting_internal (s, 0, NULL, 0, SETTING_LIVE | (flags & SETTING_FIX));  /* Defaulting logic */
      if (e && *e)
         ESP_LOGE (TAG, "Setting %s failed %s", s->name, e);
      else
         ESP_LOGD (TAG, "Setting %s created", s->name);
   }
}

static int
nvs_get (setting_t * s, const char *tag, void *data, size_t len)
{                               /* Low level get logic, returns < 0 if error.Calls the right nvs get function for type of setting */
   esp_err_t err;
   if (s->flags & SETTING_BINDATA)
   {
      if (s->size || !data)
      {                         // Fixed size, or getting len
         if ((err = nvs_get_blob (s->nvs, tag, data, &len)) != ERR_OK)
            return -err;
         if (!s->size)
            len += sizeof (revk_bindata_t);
         return len;
      }
      len -= sizeof (revk_bindata_t);
      revk_bindata_t *d = data;
      d->len = len;
      if ((err = nvs_get_blob (s->nvs, tag, d->data, &len)) != ERR_OK)
         return -err;
      return len + sizeof (revk_bindata_t);
   }
   if (s->size == 0)
   {                            /* String */
      if ((err = nvs_get_str (s->nvs, tag, data, &len)) != ERR_OK)
         return -err;
      return len;
   }
   uint64_t temp;
   if (!data)
      data = &temp;
   if (s->flags & SETTING_SIGNED)
   {
      if (s->size == 8)
      {                         /* int64 */
         if ((err = nvs_get_i64 (s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 8;
      }
      if (s->size == 4)
      {                         /* int32 */
         if ((err = nvs_get_i32 (s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 4;
      }
      if (s->size == 2)
      {                         /* int16 */
         if ((err = nvs_get_i16 (s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 2;
      }
      if (s->size == 1)
      {                         /* int8 */
         if ((err = nvs_get_i8 (s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 1;
      }
   } else
   {
      if (s->size == 8)
      {                         /* uint64 */
         if ((err = nvs_get_u64 (s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 8;
      }
      if (s->size == 4)
      {                         /* uint32 */
         if ((err = nvs_get_u32 (s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 4;
      }
      if (s->size == 2)
      {                         /* uint16 */
         if ((err = nvs_get_u16 (s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 2;
      }
      if (s->size == 1)
      {                         /* uint8 */
         if ((err = nvs_get_u8 (s->nvs, tag, data)) != ERR_OK)
            return -err;
         return 1;
      }
   }
   return -999;
}

static esp_err_t
nvs_set (setting_t * s, const char *tag, void *data)
{                               /* Low level set logic, returns < 0 if error. Calls the right nvs set function for type of setting */
   if (s->flags & SETTING_BINDATA)
   {
      if (s->size)
         return nvs_set_blob (s->nvs, tag, data, s->size);      // Fixed size - just store
      // Variable size, store the size it is
      revk_bindata_t *d = data;
      return nvs_set_blob (s->nvs, tag, d->data, d->len);       // Variable
   }
   if (s->size == 0)
      return nvs_set_str (s->nvs, tag, data);
   if (s->flags & SETTING_SIGNED)
   {
      if (s->size == 8)
         return nvs_set_i64 (s->nvs, tag, *((int64_t *) data));
      if (s->size == 4)
         return nvs_set_i32 (s->nvs, tag, *((int32_t *) data));
      if (s->size == 2)
         return nvs_set_i16 (s->nvs, tag, *((int16_t *) data));
      if (s->size == 1)
         return nvs_set_i8 (s->nvs, tag, *((int8_t *) data));
   } else
   {
      if (s->size == 8)
         return nvs_set_u64 (s->nvs, tag, *((uint64_t *) data));
      if (s->size == 4)
         return nvs_set_u32 (s->nvs, tag, *((uint32_t *) data));
      if (s->size == 2)
         return nvs_set_u16 (s->nvs, tag, *((uint16_t *) data));
      if (s->size == 1)
         return nvs_set_u8 (s->nvs, tag, *((uint8_t *) data));
   }
   ESP_LOGE (TAG, "Not saved setting %s", tag);
   return -1;
}

static const char *
revk_setting_internal (setting_t * s, unsigned int len, const unsigned char *value, unsigned char index, int flags)
{                               // Value is expected to already be binary if using binary
   flags |= s->flags;
   {                            // Overlap check
      setting_t *q;
      for (q = setting; q && q->data != s->data; q = q->next);
      if (q)
         s = q;
   }
   void *data = s->data;
   if (s->array)
   {
      if (index >= s->array)
         return "Bad index";
      if (s->array && index && !(flags & SETTING_BOOLEAN))
         data += index * (s->size ? : sizeof (void *));
   }
   // TODO we should not have suffix on index 1, that is just silly, but change needs backwards compatibility...
   char tag[16];                /* Max NVS name size */
   if (snprintf (tag, sizeof (tag), s->array ? "%s%u" : "%s", s->name, index + 1) >= sizeof (tag))
   {
      ESP_LOGE (TAG, "Setting %s%u too long", s->name, index + 1);
      return "Setting name too long";
   }
   ESP_LOGD (TAG, "MQTT setting %s (%d)", tag, len);
   char erase = 0;
   /* Using default, so remove from flash (as defaults may change later, don't store the default in flash) */
   unsigned char *temp = NULL;  // Malloc'd space to be freed
   const char *defval = s->defval;
   if (defval && (flags & SETTING_BITFIELD))
   {                            /* default is after bitfields and a space */
      while (*defval && *defval != ' ')
         defval++;
      if (*defval == ' ')
         defval++;
   }
   if (defval && *defval == '"')
      defval++;                 // Bodge
   if (defval && index)
   {
      if (!s->size)
         defval = NULL;         // Not first value so no def if a string
      else
      {                         // Allow space separated default if not a string
         int i = index;
         while (i-- && *defval)
         {
            while (*defval && *defval != ' ')
               defval++;
            if (*defval == ' ')
               defval++;
         }
         if (!*defval)
            defval = NULL;      // No def if no more values
      }
   }
   if (!len && defval && !value)
   {                            /* Use default value */
      if (s->flags & SETTING_BINDATA)
      {                         // Convert to binary
         jo_t j = jo_create_alloc ();
         jo_string (j, NULL, defval);
         jo_rewind (j);
         int l;
         if (s->flags & SETTING_HEX)
         {
            l = jo_strncpy16 (j, NULL, 0);
            if (l > 0)
               jo_strncpy16 (j, temp = mallocspi (l), l);
         } else
         {
            l = jo_strncpy64 (j, NULL, 0);
            if (l > 0)
               jo_strncpy64 (j, temp = mallocspi (l), l);
         }
         value = temp;          // temp gets freed at end
         len = l;
         jo_free (&j);
      } else
      {
         len = strlen (defval);
         value = (const unsigned char *) defval;
      }
      erase = 1;
   }
   if (!value)
   {
      defval = "";
      value = (const unsigned char *) defval;
      erase = 1;
   } else
      s->set = 1;
#ifdef SETTING_DEBUG
   if (s->flags & SETTING_BINDATA)
      ESP_LOGI (TAG, "%s=(%d bytes)", (char *) tag, len);
   else
      ESP_LOGI (TAG, "%s=%.*s", (char *) tag, len, (char *) value);
#endif
   const char *parse (void)
   {
      /* Parse new setting */
      unsigned char *n = NULL;
      int l = len;
      if (flags & SETTING_BINDATA)
      {                         /* Blob */
         unsigned char *o;
         if (!s->size)
         {                      /* Dynamic */
            l += sizeof (revk_bindata_t);
            revk_bindata_t *d = mallocspi (l);
            o = n = (void *) d;
            if (o)
            {
               d->len = len;
               if (len)
                  memcpy (d->data, value, len);
            }
         } else
         {                      // Fixed size binary
            if (l && l != s->size)
               return "Wrong size";
            o = n = mallocspi (s->size);
            if (o)
            {
               if (l)
                  memcpy (o, value, l);
               else
                  memset (o, 0, l = s->size);
            }
         }
      } else if (!s->size)
      {                         /* String */
         l++;
         n = mallocspi (l);     /* One byte for null termination */
         if (len)
            memcpy (n, value, len);
         n[len] = 0;
      } else
      {                         /* Numeric */
         uint64_t v = 0;
         if (flags & SETTING_BOOLEAN)
         {                      /* Boolean */
            if (s->size == 1)
               v = *(uint8_t *) data;
            else if (s->size == 2)
               v = *(uint16_t *) data;
            else if (s->size == 4)
               v = *(uint32_t *) data;
            else if (s->size == 8)
               v = *(uint64_t *) data;
            if (len && strchr ("YytT1", *value))
               v |= (1ULL << index);
            else
               v &= ~(1ULL << index);
         } else
         {
            char neg = 0;
            int bits = s->size * 8;
            uint64_t bitfield = 0;
            if (flags & SETTING_SET)
            {                   /* Set top bit if a value is present */
               bits--;
               if (len && *value != ' ' && *value != '"')
                  bitfield |= (1ULL << bits);
            }
            if ((flags & SETTING_BITFIELD) && s->defval)
            {                   /* Bit fields */
               while (len)
               {
                  const char *c = s->defval;
                  while (*c && *c != ' ' && *c != *value)
                     c++;
                  if (*c != *value)
                     break;
                  uint64_t m = (1ULL << (bits - 1 - (c - s->defval)));
                  if (bitfield & m)
                     break;
                  bitfield |= m;
                  len--;
                  value++;
               }
               const char *c = s->defval;
               while (*c && *c != ' ')
                  c++;
               bits -= (c - s->defval);
            }
            if (len && bits <= 0)
               return "Extra data on end";
            if (len > 2 && *value == '0' && value[1] == 'x')
            {
               flags |= SETTING_HEX;
               len -= 2;
               value += 2;
            }
            if (len && *value == '-' && (flags & SETTING_SIGNED))
            {                   /* Decimal */
               len--;
               value++;
               neg = 1;
            }
            if (flags & SETTING_HEX)
               while (len && isxdigit (*value))
               {                /* Hex */
                  uint64_t n = v * 16 + (isalpha (*value) ? 9 : 0) + (*value & 15);
                  if (n < v)
                     return "Silly number";
                  v = n;
                  value++;
                  len--;
            } else
               while (len && isdigit (*value))
               {
                  uint64_t n = v * 10 + (*value++ - '0');
                  if (n < v)
                     return "Silly number";
                  v = n;
                  len--;
               }
            if (len && *value != ' ' && *value != '"')
               return "Bad number";     // Allow space - used for multiple defval
            if (flags & SETTING_SIGNED)
               bits--;
            if (bits < 0 || (bits < 64 && ((v - (v && neg ? 1 : 0)) >> bits)))
               return "Number too big";
            if (neg)
               v = -v;
            if (flags & SETTING_SIGNED)
               bits++;
            if (bits < 64)
               v &= (1ULL << bits) - 1;
            v |= bitfield;
         }
         if (flags & SETTING_SIGNED)
         {
            if (s->size == 8)
               *((int64_t *) (n = mallocspi (l = 8))) = v;
            else if (s->size == 4)
               *((int32_t *) (n = mallocspi (l = 4))) = v;
            else if (s->size == 2)
               *((int16_t *) (n = mallocspi (l = 2))) = v;
            else if (s->size == 1)
               *((int8_t *) (n = mallocspi (l = 1))) = v;
         } else
         {
            if (s->size == 8)
               *((int64_t *) (n = mallocspi (l = 8))) = v;
            else if (s->size == 4)
               *((int32_t *) (n = mallocspi (l = 4))) = v;
            else if (s->size == 2)
               *((int16_t *) (n = mallocspi (l = 2))) = v;
            else if (s->size == 1)
               *((int8_t *) (n = mallocspi (l = 1))) = v;
         }
      }
      if (!n)
         return "Bad setting type";
      /* See if setting has changed */
      int o = nvs_get (s, tag, NULL, 0);        // Get length
#ifdef SETTING_DEBUG
      if (o < 0 && o != -ESP_ERR_NVS_NOT_FOUND)
         ESP_LOGI (TAG, "Setting %s nvs read fail %s", tag, esp_err_to_name (-o));
#endif
      if (o != l)
      {
#if defined(SETTING_DEBUG) || defined(SETTING_CHANGED)
         if (o >= 0)
            ESP_LOGI (TAG, "Setting %s different len %d/%d", tag, o, l);
#endif
         o = -1;                /* Different size */
      }
      if (o > 0)
      {
         unsigned char *d = mallocspi (l);
         if (nvs_get (s, tag, d, l) != o)
         {
            freez (n);
            freez (d);
            return "Bad setting get";
         }
         if (memcmp (n, d, o))
         {
#if defined(SETTING_DEBUG) || defined(SETTING_CHANGED)
            ESP_LOGI (TAG, "Setting %s different content %d (%02X%02X%02X%02X/%02X%02X%02X%02X)", tag, o, d[0],
                      d[1], d[2], d[3], n[0], n[1], n[2], n[3]);
#endif
            o = -1;             /* Different content */
         }
         freez (d);
      }
      if (o < 0)
      {                         /* Flash changed */
         if (erase && !(flags & SETTING_FIX))
         {
            esp_err_t __attribute__((unused)) err = nvs_erase_key (s->nvs, tag);
            if (err == ESP_ERR_NVS_NOT_FOUND)
               o = 0;
#if defined(SETTING_DEBUG) || defined(SETTING_CHANGED)
            else
               ESP_LOGI (TAG, "Setting %s erased", tag);
#endif
         } else
         {
            if (nvs_set (s, tag, n) != ERR_OK && (nvs_erase_key (s->nvs, tag) != ERR_OK || nvs_set (s, tag, n) != ERR_OK))
            {
               freez (n);
               return "Unable to store";
            }
#if defined(SETTING_DEBUG) || defined(SETTING_CHANGED)
            if (flags & SETTING_BINDATA)
               ESP_LOGI (TAG, "Setting %s stored (%d)", tag, len);
            else
               ESP_LOGI (TAG, "Setting %s stored %.*s", tag, len, value);
#endif
         }
         revk_nvs_time = uptime () + 60;
      }
      if (flags & SETTING_LIVE)
      {                         /* Store changed value in memory live */
         if (!s->size)
         {                      /* Dynamic */
            void *o = *((void **) data);
            /* See if different */
            if (!o || ((flags & SETTING_BINDATA) ? memcmp (o, n, len) : strcmp (o, (char *) n)))
            {
               *((void **) data) = n;
               freez (o);
            } else
               freez (n);       /* No change */
         } else
         {                      /* Static (try and make update atomic) */
            if (s->size == 1)
               *(uint8_t *) data = *(uint8_t *) n;
            else if (s->size == 2)
               *(uint16_t *) data = *(uint16_t *) n;
            else if (s->size == 4)
               *(uint32_t *) data = *(uint32_t *) n;
            else if (s->size == 8)
               *(uint64_t *) data = *(uint64_t *) n;
            else
               memcpy (data, n, s->size);
            freez (n);
         }
      } else if (o < 0)
         revk_restart (5, "Settings changed");
      return NULL;
   }
   const char *fail = parse ();
   freez (temp);
   return fail;                 /* OK */
}

const char *
revk_setting_dump (int level)
{                               // Dump settings (in JSON)
   level = level;
   const char *err = NULL;
   jo_t j = NULL;
   void send (void)
   {                            // Sends the settings - this deliberately uses the revk_id not the hostname as it is "seen" by any device listening
      if (!j)
         return;
      char *topic = revk_topic (topicsetting, revk_id, NULL);
      if (topic)
      {
         revk_mqtt_send (NULL, 0, topic, &j);
         free (topic);
      }
   }
   int maxpacket = MQTT_MAX;
   maxpacket -= 50;             // for headers
#ifdef	CONFIG_REVK_MESH
   maxpacket -= MESH_PAD;
#endif
   char *buf = mallocspi (maxpacket);   // Allows for topic, header, etc
   if (!buf)
      return "malloc";
   const char *hasdef (setting_t * s)
   {
      const char *d = s->defval;
      if (!d)
         return NULL;
      if (s->flags & SETTING_BITFIELD)
      {
         while (*d && *d != ' ')
            d++;
         if (*d == ' ')
            d++;
      }
      if (!*d)
         return NULL;
      if ((s->flags & SETTING_BOOLEAN) && !strchr ("YytT1", *d))
         return NULL;
      if (s->size && !strcmp (d, "0"))
         return NULL;
      return d;
   }
   int isempty (setting_t * s, int n)
   {                            // Check empty
      if (s->flags & SETTING_BOOLEAN)
      {                         // This is basically testing it is false
         uint64_t v = 0;
         if (s->size == 1)
            v = *(uint8_t *) s->data;
         else if (s->size == 2)
            v = *(uint16_t *) s->data;
         else if (s->size == 4)
            v = *(uint32_t *) s->data;
         else if (s->size == 8)
            v = *(uint64_t *) s->data;
         if (v & (1ULL << n))
            return 0;
         return 1;              // Empty bool
      }
      void *data = s->data + (s->size ? : sizeof (void *)) * n;
      int q = s->size;
      if (!q)
      {
         char *p = *(char **) data;
         if (!p || !*p)
            return 2;           // Empty string
         return 0;
      }
      while (q && !*(char *) data)
      {
         q--;
         data++;
      }
      if (!q)
         return 3;              // Empty value
      return 0;
   }
   setting_t *s;
   for (s = setting; s; s = s->next)
   {
      if ((!(s->flags & SETTING_SECRET) || s->parent) && !s->child)
      {
         int max = 0;
         if (s->array)
         {                      // Work out m - for now, parent items in arrays have to be set for rest to be output - this is the rule...
            max = s->array;
            if (!(s->flags & SETTING_BOOLEAN))
               while (max && isempty (s, max - 1))
                  max--;
         }
         jo_t p = NULL;
         void start (void)
         {
            if (!p)
            {
               if (j)
                  p = jo_copy (j);
               else
               {
                  p = jo_create_mem (buf, maxpacket);
                  jo_object (p, NULL);
               }
            }
         }
         const char *failed (void)
         {
            err = NULL;
            if (p && (err = jo_error (p, NULL)))
               jo_free (&p);    // Did not fit
            return err;
         }
         void addvalue (setting_t * s, const char *tag, int n)
         {                      // Add a value
            start ();
            void *data = s->data;
            const char *defval = s->defval ? : "";
            if (!(s->flags & SETTING_BOOLEAN))
               data += (s->size ? : sizeof (void *)) * n;
            if (s->flags & SETTING_BINDATA)
            {                   // Binary data
               int len = s->size;
               if (!len)
               {                // alloc'd with len at start
                  revk_bindata_t *d = *(void **) data;
                  len = d->len;
                  data = d->data;
               }
               if (s->flags & SETTING_HEX)
                  jo_base16 (p, tag, data, len);
               else
                  jo_base64 (p, tag, data, len);
            } else if (!s->size)
            {
               char *v = *(char **) data;
               if (v)
               {
                  jo_string (p, tag, v);        // String
               } else
                  jo_null (p, tag);     // Null string - should not happen
            } else
            {
               uint64_t v = 0;
               if (s->size == 1)
                  v = *(uint8_t *) data;
               else if (s->size == 2)
                  v = *(uint16_t *) data;
               else if (s->size == 4)
                  v = *(uint32_t *) data;
               else if (s->size == 8)
                  v = *(uint64_t *) data;
               if (s->flags & SETTING_BOOLEAN)
               {
                  jo_bool (p, tag, (v >> n) & 1);
               } else
               {                // numeric
                  char temp[100],
                   *t = temp;
                  uint8_t bits = s->size * 8;
                  if (s->flags & SETTING_SET)
                     bits--;
                  if (!(s->flags & SETTING_SET) || ((v >> bits) & 1))
                  {
                     if (s->flags & SETTING_BITFIELD)
                     {
                        while (*defval && *defval != ' ')
                        {
                           bits--;
                           if ((v >> bits) & 1)
                              *t++ = *defval;
                           defval++;
                        }
                        if (*defval == ' ')
                           defval++;
                     }
                     if (s->flags & SETTING_SIGNED)
                     {
                        bits--;
                        if ((v >> bits) & 1)
                        {
                           *t++ = '-';
                           v = (v ^ ((1ULL << bits) - 1)) + 1;
                        }
                     }
                     v &= ((1ULL << bits) - 1);
                     if (s->flags & SETTING_HEX)
                        t += sprintf (t, "%llX", v);
                     else if (bits)
                        t += sprintf (t, "%llu", v);
                  }
                  *t = 0;
                  t = temp;
                  if (*t == '-')
                     t++;
                  if (*t == '0')
                     t++;
                  else
                     while (*t >= '0' && *t <= '9')
                        t++;
                  if (t == temp || *t || (s->flags & SETTING_HEX))
                     jo_string (p, tag, temp);
                  else
                     jo_lit (p, tag, temp);
               }
            }
         }
         void addsub (setting_t * s, const char *tag, int n)
         {                      // n is 0 based
            if (s->parent)
            {
               if (!tag || (!n && hasdef (s)) || !isempty (s, n))
               {
                  start ();
                  jo_object (p, tag);
                  setting_t *q;
                  for (q = setting; q; q = q->next)
                     if (q->child && !strncmp (q->name, s->name, s->namelen))
                        if ((!n && hasdef (q)) || !isempty (q, n))
                           addvalue (q, q->name + s->namelen, n);
                  jo_close (p);
               }
            } else
               addvalue (s, tag, n);
         }
         void addsetting (void)
         {                      // Add a whole setting
            if (s->parent)
            {
               if (s->array)
               {                // Array above
                  if (max || hasdef (s))
                  {
                     start ();
                     jo_array (p, s->name);
                     for (int n = 0; n < max; n++)
                        addsub (s, NULL, n);
                     jo_close (p);
                  }
               } else
                  addsub (s, s->name, 0);
            } else if (s->array)
            {
               if (max || hasdef (s))
               {
                  start ();
                  jo_array (p, s->name);
                  for (int n = 0; n < max; n++)
                     addvalue (s, NULL, n);
                  jo_close (p);
               }
            } else if (hasdef (s) || !isempty (s, 0))
               addvalue (s, s->name, 0);
         }
         addsetting ();
         if (failed () && j)
         {
            send ();            // Failed, clear what we were sending and try again
            addsetting ();
         }
         if (failed () && s->array)
         {                      // Failed, but is an array, so try each setting individually
            for (int n = 0; n < max; n++)
            {
               char *tag;
               asprintf (&tag, "%s%d", s->name, n + 1);
               if (tag)
               {
                  addsub (s, tag, n);
                  if (failed () && j)
                  {
                     send ();   // Failed, clear what we were sending and try again
                     addsub (s, tag, n);
                  }
                  if (!failed ())
                  {             // Fitted, move forward
                     jo_free (&j);
                     j = p;
                  } else
                  {
                     jo_t j = jo_make (NULL);
                     jo_string (j, "description", "Setting did not fit");
                     jo_string (j, topicsetting, tag);
                     if (err)
                        jo_string (j, "reason", err);
                     revk_error (TAG, &j);
                  }
                  freez (tag);
               }
            }
         }
         if (!failed ())
         {                      // Fitted, move forward
            if (p)
            {
               jo_free (&j);
               j = p;
            }
         } else
         {
            jo_t j = jo_make (NULL);
            jo_string (j, "description", "Setting did not fit");
            jo_string (j, topicsetting, s->name);
            if (err)
               jo_string (j, "reason", err);
            revk_error (TAG, &j);
         }
      }
   }
   send ();
   free (buf);
   return NULL;
}

const char *
revk_settings_store (jo_t j, const char **locationp, uint8_t flags)
{
   const char *location = NULL;
   jo_rewind (j);
   if (jo_here (j) != JO_OBJECT)
      return "Not an object";
   const char *er = NULL;
#ifdef  CONFIG_REVK_OLD_SETTINGS
   int index = 0;
   int match (setting_t * s, const char *tag)
   {
      const char *a = s->name;
      const char *b = tag;
      while (*a && *a == *b)
      {
         a++;
         b++;
      }
      if (*a)
         return 1;              /* not matched whole name, no match */
      if (!*b)
      {
         index = 0;
         return 0;              /* Match, no index */
      }
      if (!s->array && *b)
         return 2;              /* not array, and more characters, no match */
      int v = 0;
      while (isdigit ((int) (*b)))
         v = v * 10 + (*b++) - '0';
      if (*b)
         return 3;              /* More on end after any digits, no match */
      if (!v || v > s->array)
         return 4;              /* Invalid index, no match */
      index = v - 1;
      return 0;                 /* Match, index */
   }
   jo_type_t t = jo_next (j);   // Start object
   while (t == JO_TAG)
   {
      location = jo_debug (j);
#ifdef SETTING_DEBUG
      ESP_LOGI (TAG, "Setting: %.10s", jo_debug (j));
#endif
      int l = jo_strlen (j);
      if (l < 0)
         break;
      char *tag = mallocspi (l + 1);
      if (!tag)
         er = "Malloc";
      else
      {
         l = jo_strncpy (j, (char *) tag, l + 1);
         t = jo_next (j);       // the value
         setting_t *s;
         for (s = setting; s && match (s, tag); s = s->next);
         if (!s)
         {
            ESP_LOGI (TAG, "Unknown %s %.20s", tag, jo_debug (j));
            er = "Unknown setting";
            t = jo_skip (j);
         } else
         {
            void store (setting_t * s)
            {
               if (s->dup)
                  for (setting_t * q = setting; q; q = q->next)
                     if (!q->dup && q->data == s->data)
                     {
                        s = q;
                        break;
                     }
#ifdef SETTING_DEBUG
               if (s->array)
                  ESP_LOGI (TAG, "Store %s[%d] (type %d): %.20s", s->name, index, t, jo_debug (j));
               else
                  ESP_LOGI (TAG, "Store %s (type %d): %.20s", s->name, t, jo_debug (j));
#endif
               int l = 0;
               char *val = NULL;
               if (t == JO_NUMBER || t == JO_STRING || t >= JO_TRUE)
               {
                  if (t == JO_STRING && (s->flags & SETTING_BINDATA))
                  {
                     if (s->flags & SETTING_HEX)
                     {
                        l = jo_strncpy16 (j, NULL, 0);
                        if (l)
                           jo_strncpy16 (j, val = mallocspi (l), l);
                     } else
                     {
                        l = jo_strncpy64 (j, NULL, 0);
                        if (l)
                           jo_strncpy64 (j, val = mallocspi (l), l);
                     }
                  } else
                  {
                     l = jo_strlen (j);
                     if (l >= 0)
                        jo_strncpy (j, val = mallocspi (l + 1), l + 1);
                  }
                  er = revk_setting_internal (s, l, (const unsigned char *) (val ? : ""), index, 0);
               } else if (t == JO_NULL)
                  er = revk_setting_internal (s, 0, NULL, index, 0);    // Factory
               else
                  er = "Bad data type";
               freez (val);
            }
            void zap (setting_t * s)
            {                   // Erasing
               if (s->dup)
                  return;
#ifdef SETTING_DEBUG
               ESP_LOGI (TAG, "Zap %s[%d]", s->name, index);
#endif
               er = revk_setting_internal (s, 0, NULL, index, 0);       // Factory default
            }
            void storesub (void)
            {
               setting_t *q;
               for (q = setting; q; q = q->next)
                  if (q->child && q->namelen > s->namelen && !strncmp (s->name, q->name, s->namelen))
                     q->used = 0;
               t = jo_next (j); // In to object
               while (t && t != JO_CLOSE && !er)
               {
                  if (t == JO_TAG)
                  {
                     int l2 = jo_strlen (j);
                     char *tag2 = mallocspi (s->namelen + l2 + 1);
                     if (tag2)
                     {
                        strcpy (tag2, s->name);
                        jo_strncpy (j, (char *) tag2 + s->namelen, l2 + 1);
                        t = jo_next (j);        // To value
                        for (q = setting; q && (!q->child || strcmp (q->name, tag2)); q = q->next);
                        if (!q)
                        {
                           ESP_LOGI (TAG, "Unknown %s %.20s", tag2, jo_debug (j));
                           er = "Unknown setting";
                        } else
                        {
                           q->used = 1;
                           store (q);
                        }
                        freez (tag2);
                     }
                  }
                  t = jo_skip (j);
               }
               for (q = setting; q; q = q->next)
                  if (!q->used && q->child && q->namelen > s->namelen && !strncmp (s->name, q->name, s->namelen))
                     zap (q);
            }
            if (t == JO_OBJECT)
            {
               if (!s->parent)
                  er = "Unexpected object";
               else
                  storesub ();
            } else if (t == JO_ARRAY)
            {
               if (!s->array)
                  er = "Not an array";
               else
               {
                  t = jo_next (j);      // In to array
                  while (index < s->array && t != JO_CLOSE && !er)
                  {
                     if (t == JO_OBJECT)
                        storesub ();
                     else if (t == JO_ARRAY)
                        er = "Unexpected array";
                     else
                        store (s);
                     t = jo_next (j);
                     index++;
                  }
                  while (index < s->array)
                  {
                     zap (s);
                     if (s->parent)
                        for (setting_t * q = setting; q; q = q->next)
                           if (q->child && q->namelen > s->namelen && !strncmp (s->name, q->name, s->namelen))
                              zap (q);
                     index++;
                  }
               }
            } else
               store (s);
            t = jo_next (j);
         }
         freez (tag);
      }
   }
#endif
   if (locationp)
      *locationp = location;
   return er ? : "";
}

void
revk_settings_commit (void)
{
   REVK_ERR_CHECK (nvs_commit (revk_nvs));
}

#endif
