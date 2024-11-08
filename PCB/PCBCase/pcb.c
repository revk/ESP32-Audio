// KiCad PCB file handling

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <err.h>
#include <math.h>
#include "pcb.h"

  /* strings from file, lots of common, so make a table */
static int strn = 0;
static const char **strs = NULL;        /* the object tags */
static const char *
add_string (const char *s, const char *e)
{                               /* allocates a string */
   if (!s)
      return NULL;
   if (!e)
      e = s + strlen (s);
   /* simplistic */
   int n;
   for (n = 0; n < strn; n++)
      if (strlen (strs[n]) == (int) (e - s) && !memcmp (strs[n], s, (int) (e - s)))
         return strs[n];
   strs = realloc (strs, (++strn) * sizeof (*strs));
   if (!strs)
      errx (1, "malloc");
   strs[n] = strndup (s, (int) (e - s));
   return strs[n];
}

static pcb_t *
parse_obj (const char **pp, const char *e)
{                               /* Scan an object */
   const char *p = *pp;
   pcb_t *pcb = malloc (sizeof (*pcb));
   if (p >= e)
      errx (1, "EOF");
   memset (pcb, 0, sizeof (*pcb));
   if (*p != '(')
      errx (1, "Expecting (\n%.20s\n", p);
   p++;
   if (p >= e)
      errx (1, "EOF");
   /* tag */
   const char *t = p;
   while (p < e && (isalnum (*p) || *p == '_'))
      p++;
   if (p == t)
      errx (1, "Expecting tag\n%.20s\n", t);
   pcb->tag = add_string (t, p);
   /* values */
   while (p < e)
   {
      while (p < e && isspace (*p))
         p++;
      if (*p == ')')
         break;
      pcb->values = realloc (pcb->values, (++(pcb->valuen)) * sizeof (*pcb->values));
      if (!pcb->values)
         errx (1, "malloc");
      pcb_val_t *value = pcb->values + pcb->valuen - 1;
      memset (value, 0, sizeof (*value));
      /* value */
      if (*p == '(')
      {
         value->isobj = 1;
         value->obj = parse_obj (&p, e);
         continue;
      }
      if (*p == '"')
      {                         /* quoted text */
         p++;
         t = p;
         while (p < e && *p != '"')
         {
            if (*p == '\\' && p[1])
               p++;
            p++;
         }
         if (p == e)
            errx (1, "EOF");
         value->istxt = 1;
         value->txt = add_string (t, p);
         p++;
         continue;
      }
      t = p;
      while (p < e && *p != ')' && !isspace (*p))
         p++;
      if (p == e)
         errx (1, "EOF");
      /* work out some basic types */
      if ((p - t) == 4 && !memcmp (t, "true", (int) (p - t)))
      {
         value->isbool = 1;
         value->istrue = 1;
         continue;;
      }
      if ((p - t) == 5 && !memcmp (t, "false", (int) (p - t)))
      {
         value->isbool = 1;
         continue;;
      }
      /* does it look like a value number */
      const char *q = t;
      if (q < p && *q == '-')
         q++;
      while (q < p && isdigit (*q))
         q++;
      if (q < p && *q == '.')
      {
         q++;
         while (q < p && isdigit (*q))
            q++;
      }
      if (q == p)
      {                         /* seems legit */
         char *val = strndup (t, q - t);
         double v = 0;
         if (sscanf (val, "%lf", &v) == 1)
         {                      /* safe as we know followed by space or close bracket and not EOF */
            value->isnum = 1;
            value->num = v;
            free (val);
            continue;
         }
         free (val);
      }
      /* assume string */
      value->islit = 1;
      value->txt = add_string (t, p);
   }
   if (p >= e)
      errx (1, "EOF");
   if (*p != ')')
      errx (1, "Expecting )\n%.20s\n", p);
   p++;
   while (p < e && isspace (*p))
      p++;
   *pp = p;
   return pcb;
}

static void
pcb_stream (FILE * o, pcb_t * pcb, int l)
{                               // Write a pcb
   if (!pcb->tag)
      return;                   // Marked as delete
   char sub = 0;
   void nl (void)
   {
      fputc ('\n', o);
      for (int q = 0; q < l; q++)
         fputc (' ', o);
   }
   if (l)
      nl ();
   fprintf (o, "(%s", pcb->tag);
   for (int n = 0; n < pcb->valuen; n++)
   {
      fputc (' ', o);
      pcb_val_t *v = &pcb->values[n];
      if (v->isobj)
      {
         sub = 1;
         pcb_stream (o, v->obj, l + 1);
      } else if (v->islit)
         fprintf (o, "%s", v->txt);
      else if (v->istxt)
         fprintf (o, "\"%s\"", v->txt);
      else if (v->isnum)
      {
         if (v->num == round (v->num))
            fprintf (o, "%.0lf", v->num);
         else
            fprintf (o, "%lf", v->num);
      } else if (v->isbool)
         fprintf (o, "%s", v->istrue ? "true" : "false");
   }
   if (sub)
      nl ();
   fprintf (o, ")");
}

void
pcb_write (const char *pcbfile, pcb_t * pcb)
{
   FILE *o = stdout;
   if (pcbfile && *pcbfile && strcmp (pcbfile, "-"))
      o = fopen (pcbfile, "w");
   if (!o)
      errx (1, "Cannot open %s", pcbfile);
   pcb_stream (o, pcb, 0);
   fclose (o);
}

pcb_t *
pcb_find (pcb_t * pcb, const char *tag, pcb_t * prev)
{                               // Find a tag
   int n = 0;
   if (prev)
      for (; n < pcb->valuen; n++)
         if (pcb->values[n].isobj && pcb->values[n].obj == prev)
         {
            n++;
            break;
         }
   for (; n < pcb->valuen; n++)
      if (pcb->values[n].isobj && pcb->values[n].obj->tag && !strcmp (pcb->values[n].obj->tag, tag))
         return pcb->values[n].obj;
   return NULL;
}

pcb_t *
pcb_load (const char *pcbfile)
{                               // Load PCB from file
   int f = open (pcbfile, O_RDONLY);
   if (f < 0)
      err (1, "Cannot open %s", pcbfile);
   struct stat s;
   if (fstat (f, &s))
      err (1, "Cannot stat %s", pcbfile);
   char *data = mmap (NULL, s.st_size, PROT_READ, MAP_PRIVATE, f, 0);
   if (!data)
      errx (1, "Cannot access %s", pcbfile);
   const char *p = data;
   pcb_t *pcb = parse_obj (&p, data + s.st_size);
   munmap (data, s.st_size);
   close (f);
   return pcb;
}

void
pcb_delete (pcb_t * o)
{
   pcb_clear (o);
   free (o);
}

void
pcb_clear (pcb_t * o)
{                               // Clear values in an object
   for (int n = 0; n < o->valuen; n++)
      if (o->values[n].isobj)
         pcb_delete (o->values[n].obj);
   free (o->values);
   o->values = NULL;
   o->valuen = 0;
}

pcb_val_t *
pcb_append (pcb_t * o)
{                               // Create new value
   int n = o->valuen++;
   o->values = realloc (o->values, o->valuen * sizeof (*o->values));
   memset (&o->values[n], 0, sizeof (*o->values));
   return &o->values[n];
}

pcb_val_t *
pcb_append_num (pcb_t * o, double val)
{                               // Append value num
   pcb_val_t *v = pcb_append (o);
   v->isnum = 1;
   v->num = val;
   return v;
}

pcb_val_t *
pcb_append_lit (pcb_t * o, const char *val)
{                               // Append value lit
   pcb_val_t *v = pcb_append (o);
   v->islit = 1;
   v->txt = add_string (val, NULL);
   return v;
}

pcb_val_t *
pcb_append_txt (pcb_t * o, const char *val)
{                               // Append value txt
   pcb_val_t *v = pcb_append (o);
   v->istxt = 1;
   v->txt = add_string (val, NULL);
   return v;
}

pcb_val_t *
pcb_append_bool (pcb_t * o, int val)
{                               // Append value bool
   pcb_val_t *v = pcb_append (o);
   v->isbool = 1;
   v->istrue = val;
   return v;
}

pcb_t *
pcb_append_obj (pcb_t * o, const char *val)
{                               // Append object
   pcb_val_t *v = pcb_append (o);
   v->isobj = 1;
   v->obj = malloc (sizeof (pcb_t));
   memset (v->obj, 0, sizeof (pcb_t));
   v->obj->tag = add_string (val, NULL);
   return v->obj;
}

pcb_t *
pcb_free (pcb_t * pcb)
{                               // Erase PCB (return NULL)
   pcb_delete (pcb);
   for (int n = 0; n < strn; n++)
      free ((char *) strs[n]);
   free (strs);
   return NULL;
}
