// Settings generation too
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#include "include/revk_ctype.h"

#ifndef _WIN32
#include <err.h>
#else
#define err(n, fmt, ...) fprintf(stderr, fmt "\n", __VA_ARGS__);
#define errx(n, ...) fprintf(stderr, __VA_ARGS__)

// Very crude replacement, only enough for us here
static int
getline (char **lineptr, size_t *n, FILE * stream)
{
   static char line_buffer[1024];

   if (!fgets (line_buffer, sizeof (line_buffer), stream))
      return -1;

   *lineptr = line_buffer;
   return 1;
}

static char *
strndup (const char *str, size_t len)
{
   int l = strlen (str);
   char *dest;

   if (len < l)
      l = len;
   dest = malloc (l + 1);
   memcpy (dest, str, len);
   dest[l] = 0;
   return dest;
}

#endif

typedef struct def_s def_t;
struct def_s
{
   def_t *next;
   const char *fn;
   char *define;
   char *comment;
   char *type;
   int group;
   int decimal;
   char *name;
   char *name1;
   char *name2;
   char *def;
   char *attributes;
   char *array;
   char config:1;               // Is CONFIG_... def
   char quoted:1;               // Is quoted def
};
def_t *defs = NULL,
   *deflast = NULL;

int groups = 0;
char **group = NULL;

int
typename (FILE * O, const char *type)
{
   if (!strcmp (type, "gpio"))
      fprintf (O, "revk_gpio_t");
   else if (!strcmp (type, "blob"))
      fprintf (O, "revk_settings_blob_t*");
   else if (!strcmp (type, "s") || !strcmp (type, "json"))
      fprintf (O, "char*");
   else if (*type == 'c' && is_digit (type[1]))
      fprintf (O, "char");
   else if (*type == 'o' && is_digit (type[1]))
      fprintf (O, "uint8_t");
   else if (*type == 'u' && is_digit (type[1]))
      fprintf (O, "uint%s_t", type + 1);
   else if (*type == 's' && is_digit (type[1]))
      fprintf (O, "int%s_t", type + 1);
   else
      return 1;
   return 0;
}

void
typesuffix (FILE * O, const char *type)
{
   if (*type == 'o' && is_digit (type[1]))
      fprintf (O, "[%s]", type + 1);
   else if (*type == 'c' && is_digit (type[1]))
      fprintf (O, "[%d]", atoi (type + 1) + 1);
}

void
typeinit (FILE * O, const char *type)
{
   fprintf (O, "=");
   if (*type == 'c' && is_digit (type[1]))
      fprintf (O, "\"\"");
   else if (!strcmp (type, "gpio") || (*type == 'o' && is_digit (type[1])))
      fprintf (O, "{0}");
   else if (!strcmp (type, "blob") || !strcmp (type, "s") || !strcmp (type, "json"))
      fprintf (O, "NULL");
   else
      fprintf (O, "0");
}

int
main (int argc, const char *argv[])
{
   int debug = 0;
   int nocomment = 0;
   int noplace = 0;
   const char *cfile = "settings.c";
   const char *hfile = "settings.h";
   const char *dummysecret = "✶✶✶✶✶✶✶✶";
   const char *extension = "def";
   int maxname = 15;
   poptContext optCon;          // context for parsing command-line options
   {                            // POPT
      const struct poptOption optionsTable[] = {
         {"c-file", 'c', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &cfile, 0, "C-file", "filename"},
         {"h-file", 'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &hfile, 0, "H-file", "filename"},
         {"dummy-secret", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &dummysecret, 0, "Dummy secret", "secret"},
         {"extension", 'e', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &extension, 0, "Only handle files ending with this",
          "extension"},
         {"max-name", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &maxname, 0, "Max name len", "N"},
         {"no-comment", 0, POPT_ARG_NONE, &nocomment, 0, "No .comment"},
         {"no-place", 0, POPT_ARG_NONE, &noplace, 0, "No .place"},
         {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug"},
         POPT_AUTOHELP {}
      };

      optCon = poptGetContext (NULL, argc, argv, optionsTable, 0);
      poptSetOtherOptionHelp (optCon, "Definitions-files");

      int c;
      if ((c = poptGetNextOpt (optCon)) < -1)
         errx (1, "%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));

      if (!poptPeekArg (optCon))
      {
         poptPrintUsage (optCon, stderr, 0);
         return -1;
      }

      FILE *C = fopen (cfile, "w+");
      if (!C)
         err (1, "Cannot open %s", cfile);
      FILE *H = fopen (hfile, "w+");
      if (!H)
         err (1, "Cannot open %s", hfile);

      fprintf (C, "// Settings\n");
      fprintf (C, "// Generated from:-\n");

      fprintf (H, "// Settings\n");
      fprintf (H, "// Generated from:-\n");

      char *line = NULL;
      size_t len = 0;
      const char *fn;
      while ((fn = poptGetArg (optCon)))
      {
         char *ext = strrchr (fn, '.');
         if (!ext || strcmp (ext + 1, extension))
            continue;
         fprintf (C, "// %s\n", fn);
         fprintf (H, "// %s\n", fn);
         FILE *I = fopen (fn, "r");
         if (!I)
            err (1, "Cannot open %s", fn);
         while (getline (&line, &len, I) >= 0)
         {
            char *p;
            for (p = line + strlen (line); p > line && is_space (p[-1]); p--);
            *p = 0;
            p = line;
            while (*p && is_space (*p))
               p++;
            if (!*p)
               continue;
            def_t *d = malloc (sizeof (*d));
            memset (d, 0, sizeof (*d));
            d->fn = fn;
            if (*p == '#')
            {
               d->define = p;
            } else if (*p == '/' && p[1] == '/')
            {
               p += 2;
               while (*p && is_space (*p))
                  p++;
               d->comment = p;
            } else
            {
               d->type = p;
               while (*p && !is_space (*p))
                  p++;
               while (*p && is_space (*p))
                  *p++ = 0;
               d->name1 = p;
               while (*p && !is_space (*p) && *p != '.')
                  p++;
               if (*p == '.')
               {
                  *p++ = 0;
                  d->name2 = p;
                  while (*p && !is_space (*p))
                     p++;
               }
               while (*p && is_space (*p))
                  *p++ = 0;
               if (*p)
               {
                  if (*p == '"')
                  {             // Quoted default
                     d->quoted = 1;
                     p++;
                     d->def = p;
                     while (*p && *p != '"')
                        p++;
                     if (*p)
                        *p++ = 0;
                  } else if (*p != '.' && *p != '/' && p[1] != '/')
                  {             // Unquoted default
                     d->def = p;
                     while (*p && !is_space (*p))
                        p++;
                  }
               }
               while (*p && is_space (*p))
                  *p++ = 0;
               if (*p == '.')
               {
                  d->attributes = p;
                  while (*p == '.')
                  {
                     while (*p && !is_space (*p))
                     {
                        if (*p == '"' || *p == '\'')
                        {
                           char c = *p++;
                           while (*p && *p != c)
                           {
                              if (*p == '\\' && p[1])
                                 p++;
                              p++;
                           }
                        }
                        p++;
                     }
                     while (*p && is_space (*p))
                        p++;
                  }
               }
               if (*p == '/' && p[1] == '/')
               {
                  *p++ = 0;
                  p++;
                  while (*p && is_space (*p))
                     p++;
                  d->comment = p;
               }
            }
            if (d->type)
               d->type = strdup (d->type);
            if (d->comment)
               d->comment = strdup (d->comment);
            if (d->define)
               d->define = strdup (d->define);
            if (d->name1)
               d->name1 = strdup (d->name1);
            if (d->name2)
               d->name2 = strdup (d->name2);
            if (d->def)
            {
               d->def = strdup (d->def);
               if (!d->quoted && !strncmp (d->def, "CONFIG_", 7))
               {
                  char *p = d->def + 7;
                  while (*p && (is_alnum (*p) || *p == '_'))
                     p++;
                  if (!*p)
                     d->config = 1;
               }
            }
            if (d->attributes)
            {
               char *i = d->attributes,
                  *o = d->attributes;
               while (*i)
               {
                  if (*i == '"' || *i == '\'')
                  {
                     char c = *i;
                     *o++ = *i++;
                     while (*i && *i != c)
                     {
                        if (*i == '\\' && i[1])
                           *o++ = *i++;
                        *o++ = *i++;
                     }
                  } else if (is_space (*i))
                  {
                     while (*i && is_space (*i))
                        i++;
                     if (*i)
                        *o++ = ',';
                  } else
                     *o++ = *i++;
               }
               *o = 0;
               d->attributes = strdup (d->attributes);
               for (i = d->attributes; *i; i++)
                  if (*i == '"')
                  {
                     i++;
                     while (*i && *i != '"')
                     {
                        if (*i == '\\' && i[1])
                           i++;
                        i++;
                     }
                  } else if (!strncmp (i, ".array=", 7))
                  {
                     i += 7;
                     for (o = i; *o && *o != ','; o++);
                     d->array = strndup (i, (int) (o - i));
                  } else if (!strncmp (i, ".decimal=", 9))
                  {
                     i += 9;
                     d->decimal = atoi (i);
                  }
            }
            if (d->type && !d->name1)
               errx (1, "Missing name for %s in %s", d->type, fn);
            if (strlen (d->name1 ? : "") + strlen (d->name2 ? : "") + (d->array ? 1 : 0) > maxname)
               errx (1, "name too long for %s%s in %s", d->name1 ? : "", d->name2 ? : "", fn);
            if (d->name1)
            {
               asprintf (&d->name, "%s%s", d->name1, d->name2 ? : "");
               if (d->name2)
               {
                  int g;
                  for (g = 0; g < groups && strcmp (group[g], d->name1); g++);
                  if (g == groups)
                  {
                     groups++;
                     group = realloc (group, sizeof (*group) * groups);
                     group[g] = d->name1;
                  }
                  d->group = g + 1;     // 0 means not group
               }
            }
            if (defs)
               deflast->next = d;
            else
               defs = d;
            deflast = d;
         }
         fclose (I);
      }

      def_t *d;

      char hasblob = 0;
      char hasbit = 0;
      char hasunsigned = 0;
      char hassigned = 0;
      char hasoctet = 0;
      char hasstring = 0;
      char hasjson = 0;
      char hasgpio = 0;
      char hasold = 0;
      char hascomment = 0;
      char hasplace = 0;

      if (!nocomment)
      {
         for (d = defs; d && (!d->type || !d->comment); d = d->next);
         if (d)
            hascomment = 1;
      }

      if (!noplace)
      {
         for (d = defs; d && (!d->attributes || !strstr (d->attributes, ".place=")); d = d->next);
         if (d)
            hasplace = 1;
      }

      for (d = defs; d && (!d->attributes || !strstr (d->attributes, ".old=")); d = d->next);
      if (d)
         hasold = 1;

      for (d = defs; d && (!d->type || *d->type != 'o' || !is_digit (d->type[1])); d = d->next);
      if (d)
         hasoctet = 1;

      for (d = defs; d && (!d->type || *d->type != 's' || !is_digit (d->type[1])); d = d->next);
      if (d)
         hassigned = 1;

      for (d = defs; d && (!d->type || *d->type != 'u' || !is_digit (d->type[1])); d = d->next);
      if (d)
         hasunsigned = 1;

      for (d = defs; d && (!d->type || *d->type != 'c' || !is_digit (d->type[1])); d = d->next);
      if (!d)
         for (d = defs; d && (!d->type || strcmp (d->type, "s")); d = d->next);
      if (d)
         hasstring = 1;

      for (d = defs; d && (!d->type || strcmp (d->type, "json")); d = d->next);
      if (d)
         hasjson = 1;

      fprintf (C, "\n");
      fprintf (C, "#include <stdint.h>\n");
      fprintf (C, "#include \"sdkconfig.h\"\n");
      fprintf (C, "#include \"settings.h\"\n");

      fprintf (H, "\n");
      fprintf (H, "#include <stdint.h>\n");
      fprintf (H, "#include <stddef.h>\n");
      for (d = defs; d && (!d->attributes || !strstr (d->attributes, ".rtc=")); d = d->next);
      if (d)
         fprintf (H, "#include <esp_attr.h>\n");

      fprintf (H, "typedef struct revk_settings_s revk_settings_t;\n"   //
               "struct revk_settings_s {\n"     //
               " void *ptr;\n"  //
               " const char name[%d];\n"        //
               " const char *def;\n"    //
               " const char *flags;\n", maxname + 1);
      if (hasold)
         fprintf (H, " const char *old;\n");
      if (hascomment)
         fprintf (H, " const char *comment;\n");
      if (hasplace)
         fprintf (H, " const char *place;\n");
      fprintf (H, " uint16_t size;\n"   //
               " uint8_t group;\n"      //
               " uint8_t bit;\n"        //
               " uint8_t dot:4;\n"      //
               " uint8_t len:4;\n"      //
               " uint8_t type:3;\n"     //
               " uint8_t decimal:5;\n"  //
               " uint8_t digits:5;\n"   //
               " uint8_t array:7;\n"    //
               " uint8_t malloc:1;\n"   //
               " uint8_t revk:1;\n"     //
               " uint8_t live:1;\n"     //
               " uint8_t hide:1;\n"     //
               " uint8_t fix:1;\n"      //
               " uint8_t set:1;\n"      //
               " uint8_t hex:1;\n"      //
               " uint8_t base32:1;\n"   //
               " uint8_t base64:1;\n"   //
               " uint8_t secret:1;\n"   //
               " uint8_t dq:1;\n"       //
               " uint8_t gpio:1;\n"     //
               " uint8_t rtc:1;\n"      //
               "};\n");

      for (d = defs; d && (!d->type || strcmp (d->type, "blob")); d = d->next);
      if (d)
      {
         fprintf (H, "typedef struct revk_settings_blob_s revk_settings_blob_t;\n"      //
                  "struct revk_settings_blob_s {\n"     //
                  " uint16_t len;\n"    //
                  " uint8_t data[];\n"  //
                  "};\n");
         hasblob = 1;
      }
      for (d = defs; d && (!d->type || strcmp (d->type, "gpio")); d = d->next);
      if (d)
      {
         hasgpio = 1;
         hasunsigned = 1;       // GPIO is treated as a u16
         fprintf (H, "typedef struct revk_gpio_s revk_gpio_t;\n"        //
                  "struct revk_gpio_s {\n"      //
                  " uint16_t num:10;\n" //
                  " uint16_t strong:1;\n"       //
                  " uint16_t weak:1;\n" //
                  " uint16_t pulldown:1;\n"     //
                  " uint16_t nopull:1;\n"       //
                  " uint16_t invert:1;\n"       //
                  " uint16_t set:1;\n"  //
                  "};\n");
      }
      for (d = defs; d && (!d->type || strcmp (d->type, "bit")); d = d->next);
      if (d)
      {                         // Bit fields
         hasbit = 1;
         fprintf (H, "enum {\n");
         for (d = defs; d; d = d->next)
            if (d->define)
               fprintf (H, "%s\n", d->define);
            else if (d->type && !strcmp (d->type, "bit"))
               fprintf (H, " REVK_SETTINGS_BITFIELD_%s,\n", d->name);
         fprintf (H, "};\n");
         fprintf (H, "typedef struct revk_settings_bits_s revk_settings_bits_t;\n");
         fprintf (H, "struct revk_settings_bits_s {\n");
         fprintf (C, "revk_settings_bits_t revk_settings_bits={0};\n");
         for (d = defs; d; d = d->next)
         {
            if (d->define)
               fprintf (H, "%s\n", d->define);
            else if (d->type && !strcmp (d->type, "bit"))
            {
               fprintf (H, " uint8_t %s:1;", d->name);
               if (d->comment)
                  fprintf (H, "\t// %s", d->comment);
               fprintf (H, "\n");
            }
         }
         fprintf (H, "};\n");
         for (d = defs; d; d = d->next)
            if (d->define)
               fprintf (H, "%s\n", d->define);
            else if (d->type && !strcmp (d->type, "bit"))
               fprintf (H, "#define	%s	revk_settings_bits.%s\n", d->name, d->name);
            else if (d->type)
            {
               fprintf (H, "extern ");
               if (typename (H, d->type))
                  errx (1, "Unknown type %s in %s", d->type, d->fn);
               fprintf (H, " %s", d->name);
               if (d->array)
                  fprintf (H, "[%s]", d->array);
               typesuffix (H, d->type);
               fprintf (H, ";");
               if (d->comment)
                  fprintf (H, "\t// %s", d->comment);
               fprintf (H, "\n");
            }
         fprintf (H, "extern revk_settings_bits_t revk_settings_bits;\n");
      }

      fprintf (H, "enum {\n");
      if (hassigned)
         fprintf (H, " REVK_SETTINGS_SIGNED,\n");
      if (hasunsigned)
         fprintf (H, " REVK_SETTINGS_UNSIGNED,\n");
      if (hasbit)
         fprintf (H, " REVK_SETTINGS_BIT,\n");
      if (hasblob)
         fprintf (H, " REVK_SETTINGS_BLOB,\n");
      if (hasstring)
         fprintf (H, " REVK_SETTINGS_STRING,\n");
      if (hasjson)
         fprintf (H, " REVK_SETTINGS_JSON,\n");
      if (hasoctet)
         fprintf (H, " REVK_SETTINGS_OCTET,\n");
      fprintf (H, "};\n");
      if (hasold)
         fprintf (H, "#define	REVK_SETTINGS_HAS_OLD\n");
      if (hascomment)
         fprintf (H, "#define	REVK_SETTINGS_HAS_COMMENT\n");
      if (hasplace)
         fprintf (H, "#define	REVK_SETTINGS_HAS_PLACE\n");
      if (hasgpio)
         fprintf (H, "#define	REVK_SETTINGS_HAS_GPIO\n");
      if (hassigned || hasunsigned)
         fprintf (H, "#define	REVK_SETTINGS_HAS_NUMERIC\n");
      if (hassigned)
         fprintf (H, "#define	REVK_SETTINGS_HAS_SIGNED\n");
      if (hasunsigned)
         fprintf (H, "#define	REVK_SETTINGS_HAS_UNSIGNED\n");
      if (hasbit)
         fprintf (H, "#define	REVK_SETTINGS_HAS_BIT\n");
      if (hasblob)
         fprintf (H, "#define	REVK_SETTINGS_HAS_BLOB\n");
      if (hasstring)
         fprintf (H, "#define	REVK_SETTINGS_HAS_STRING\n");
      if (hasjson)
         fprintf (H, "#define	REVK_SETTINGS_HAS_JSON\n");
      if (hasoctet)
         fprintf (H, "#define	REVK_SETTINGS_HAS_OCTET\n");

      fprintf (C, "#define	str(s)	#s\n");
      fprintf (C, "#define	quote(s)	str(s)\n");
      fprintf (C, "revk_settings_t const revk_settings[]={\n");
      int count = 0;
      for (d = defs; d; d = d->next)
         if (d->define)
            fprintf (C, "%s\n", d->define);
         else if (d->name)
         {
            count++;
            fprintf (C, " {");
            if (d->attributes && !(*d->type == 's' || *d->type == 'u' || *d->type == 'o') && is_digit (d->type[1]))
            {                   // non numeric
               if (strstr (d->attributes, ".set=1"))
                  errx (1, ".set on non numeric for %s in %s", d->name, d->type);
               if (strstr (d->attributes, ".flags="))
                  errx (1, ".flags on non numeric for %s in %s", d->name, d->type);
               if (strstr (d->attributes, ".base64=1"))
                  errx (1, ".base64 on non numeric for %s in %s", d->name, d->type);
               if (strstr (d->attributes, ".base32=1"))
                  errx (1, ".base32 on non numeric for %s in %s", d->name, d->type);
               if (strstr (d->attributes, ".digits="))
                  errx (1, ".digits on non numeric for %s in %s", d->name, d->type);
               if (strstr (d->attributes, ".decimal=") && strstr (d->attributes, ".hex=1"))
                  errx (1, ".hex and .decimal on non numeric for %s in %s", d->name, d->type);
            }
            if (*d->type == 's' && is_digit (d->type[1]))
               fprintf (C, ".type=REVK_SETTINGS_SIGNED");
            else if (*d->type == 'u' && is_digit (d->type[1]))
               fprintf (C, ".type=REVK_SETTINGS_UNSIGNED");
            else if (!strcmp (d->type, "gpio"))
               fprintf (C, ".type=REVK_SETTINGS_UNSIGNED,.gpio=1");
            else if (!strcmp (d->type, "bit"))
               fprintf (C, ".type=REVK_SETTINGS_BIT");
            else if (!strcmp (d->type, "blob"))
               fprintf (C, ".type=REVK_SETTINGS_BLOB");
            else if (!strcmp (d->type, "json"))
               fprintf (C, ".type=REVK_SETTINGS_JSON");
            else if (!strcmp (d->type, "s") || (*d->type == 'c' && is_digit (d->type[1])))
               fprintf (C, ".type=REVK_SETTINGS_STRING");
            else if (*d->type == 'o' && is_digit (d->type[1]))
               fprintf (C, ".type=REVK_SETTINGS_OCTET");
            else
               errx (1, "Unknown type %s for %s in %s", d->type, d->name, d->fn);
            fprintf (C, ",.name=\"%s\"", d->name);
            if (hascomment && d->comment)
            {
               fprintf (C, ",.comment=\"");
               for (char *p = d->comment; *p; p++)
               {
                  if (*p == '\\' || *p == '"')
                     fputc ('\\', C);
                  fputc (*p, C);
               }
               fprintf (C, "\"");
            }
            if (d->group)
               fprintf (C, ",.group=%d", d->group);
            fprintf (C, ",.len=%d", (int) strlen (d->name));
            if (d->name2)
               fprintf (C, ",.dot=%d", (int) strlen (d->name1));
            if (!d->name2)
               for (int g = 0; g < groups; g++)
                  if (!strcmp (d->name1, group[g]))
                     errx (1, "Clash %s in %s with sub object", d->name1, d->fn);
            if (d->def)
            {
               if (d->config)
                  fprintf (C, ",.dq=1,.def=quote(%s)", d->def); // Always re quote, string def parsing assumes "
               else
               {
                  fprintf (C, ",.def=\"");
                  if (d->quoted && !strcmp (d->type, "json"))
                     fprintf (C, "\\\"");
                  for (char *p = d->def; *p; p++)
                  {
                     if (*p == '\\' || *p == '"')
                        fputc ('\\', C);
                     fputc (*p, C);
                  }
                  if (d->quoted && !strcmp (d->type, "json"))
                     fprintf (C, "\\\"");
                  fprintf (C, "\"");
               }
            }
            if (!strcmp (d->type, "bit"))
               fprintf (C, ",.bit=REVK_SETTINGS_BITFIELD_%s", d->name);
            else
            {                   // Bits are the only one without pointers
               fprintf (C, ",.ptr=&%s", d->name);
               if (!strcmp (d->type, "s") || !strcmp (d->type, "blob") || !strcmp (d->type, "json"))
                  fprintf (C, ",.malloc=1");
               else
               {                // Code allows for a .pointer and .size but none of the types we use do that at present, as all .size are fixed in situ
                  fprintf (C, ",.size=sizeof(");
                  typename (C, d->type);
                  typesuffix (C, d->type);
                  fprintf (C, ")");
               }
            }
            if (!strcmp (d->type, "gpio"))
            {
               if (!d->attributes || !strstr (d->attributes, ".fix="))
                  fprintf (C, ",.fix=1");
               fprintf (C, ",.set=1,.flags=\"- ~↓↕⇕\"");
            }
            if (d->attributes)
               fprintf (C, ",%s", d->attributes);
            fprintf (C, "},\n");
            if (!strcmp (d->type, "bit"))
            {
               if (d->array)
                  errx (1, "Cannot do bit array %s in %s", d->name, d->fn);
               if (d->attributes && strstr (d->attributes, ".rtc="))
                  errx (1, "Cannot do bit in RTC %s in %s", d->name, d->fn);
            }
            if ((!strcmp (d->type, "s") || !strcmp (d->type, "json")) && d->attributes && strstr (d->attributes, ".rtc="))
               errx (1, "Cannot do char* in RTC %s in %s", d->name, d->fn);
         }
      fprintf (C, "{0}};\n");
      fprintf (C, "#undef quote\n");
      fprintf (C, "#undef str\n");
      for (d = defs; d; d = d->next)
         if (d->define)
            fprintf (C, "%s\n", d->define);
         else if (d->type && strcmp (d->type, "bit"))
         {
            if (d->attributes && strstr (d->attributes, ".rtc="))
               fprintf (C, "RTC_NOINIT_ATTR ");
            typename (C, d->type);
            fprintf (C, " %s", d->name);
            if (d->array)
               fprintf (C, "[%s]", d->array);
            typesuffix (C, d->type);
            if (d->array)
               fprintf (C, "={0}");
            else
               typeinit (C, d->type);
            fprintf (C, ";\n");
         }
      // Final includes
      for (d = defs; d; d = d->next)
         if (d->name && d->decimal)
         {
            fprintf (H, "#define	%s_scale	1", d->name);
            for (int i = 0; i < d->decimal; i++)
               fputc ('0', H);
            fprintf (H, "\n");
         }
      fprintf (H, "typedef uint8_t revk_setting_bits_t[%d];\n", (count + 7) / 8);
      fprintf (H, "typedef uint8_t revk_setting_group_t[%d];\n", (groups + 8) / 8);
      fprintf (C, "const char revk_settings_secret[]=\"%s\";\n", dummysecret);
      fprintf (H, "extern const char revk_settings_secret[];\n");
      fclose (H);
      fclose (C);
   }
   poptFreeContext (optCon);
   return 0;
}
