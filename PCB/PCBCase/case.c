/* Make an OpendScad file from a kicad_pcb file */
/* (c) 2021-2023 Adrian Kennard Andrews & Arnold Ltd */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <popt.h>
#include <err.h>
#include <float.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include "pcb.h"

/* yes, all globals, what the hell */
int debug = 0;
int norender = 0;
int layerpcb = 0;
int layercase = 0;
int nohull = 0;
const char *pcbfile = NULL;
char *scadfile = NULL;
const char *modeldir = "PCBCase/models";
const char *ignore = NULL;
double pcbthickness = 0;
double casebottom = 5;
double casetop = 5;
double casewall = 3;
double lip = 2;                 /* Lip height offset */
double fit = 0.0;
double edge = 1;
double margin = 0.2;
double spacing = 0;
double delta = 0.01;
double hullcap = 1;
double hulledge = 1;

void
copy_file (FILE * o, const char *fn)
{
   int f = open (fn, O_RDONLY);
   if (f < 0)
      err (1, "Cannot open copy %s", fn);
   struct stat s;
   if (fstat (f, &s))
      err (1, "Cannot stat %s", fn);
   char *data = mmap (NULL, s.st_size, PROT_READ, MAP_PRIVATE, f, 0);
   if (!data)
      errx (1, "Cannot access %s", fn);
   fwrite (data, s.st_size, 1, o);
   munmap (data, s.st_size);
   close (f);
}

void
write_scad (pcb_t * pcb)
{
   pcb_t *o,
    *o2,
    *o3;
   /* making scad file */
   FILE *f = stdout;
   if (strcmp (scadfile, "-"))
      f = fopen (scadfile, "w");
   if (!f)
      err (1, "Cannot open scad %s", scadfile);

   if (chdir (modeldir))
      errx (1, "Cannot access model dir %s", modeldir);

   pcb_t *general = pcb_find (pcb, "general", NULL);
   if (general)
   {
      if ((o = pcb_find (general, "thickness", NULL)) && o->valuen == 1 && o->values[0].isnum)
         pcbthickness = o->values[0].num;
   }
   fprintf (f, "// Generated case design for %s\n", pcbfile);
   fprintf (f, "// By https://github.com/revk/PCBCase\n");
   {
      struct tm t;
      time_t now = time (0);
      localtime_r (&now, &t);
      fprintf (f, "// Generated %04d-%02d-%02d %02d:%02d:%02d\n", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
               t.tm_sec);
   }
   if ((o = pcb_find (pcb, "title_block", NULL)))
      for (int n = 0; n < o->valuen; n++)
         if (o->values[n].isobj && (o2 = o->values[n].obj)->valuen >= 1)
         {
            if (o2->values[o2->valuen - 1].istxt)
               fprintf (f, "// %s:\t%s\n", o2->tag, o2->values[o2->valuen - 1].txt);
            else if (o2->values[0].isnum)
               fprintf (f, "// %s:\t%lf\n", o2->tag, o2->values[0].num);
         }
   fprintf (f, "//\n\n");
   fprintf (f, "// Globals\n");
   fprintf (f, "margin=%lf;\n", margin);
   fprintf (f, "lip=%lf;\n", lip);
   fprintf (f, "casebottom=%lf;\n", casebottom);
   fprintf (f, "casetop=%lf;\n", casetop);
   fprintf (f, "casewall=%lf;\n", casewall);
   fprintf (f, "fit=%lf;\n", fit);
   fprintf (f, "edge=%lf;\n", edge);
   fprintf (f, "pcbthickness=%lf;\n", pcbthickness);
   fprintf (f, "nohull=%s;\n", nohull ? "true" : "false");
   fprintf (f, "hullcap=%lf;\n", hullcap);
   fprintf (f, "hulledge=%lf;\n", hulledge);
   fprintf (f, "useredge=%s;\n", layercase ? "true" : "false");

   double lx = DBL_MAX,
      hx = -DBL_MAX,
      ly = DBL_MAX,
      hy = -DBL_MAX;
   double ry;                   /* reference for Y, as it is flipped! */
   /* sanity */
   if (!pcbthickness)
      errx (1, "Specify pcb thickness");
   if (!lip)
      lip = pcbthickness / 2;
   void outline (const char *layer, const char *tag)
   {
      struct
      {
         double x1,
           y1;
         double xm,
           ym;
         double x2,
           y2;
         unsigned char arc:1;
         unsigned char used:1;
      } *cuts = NULL;
      int cutn = 0;

      void add (pcb_t * o, double dx, double dy, double a)
      {
         void makecuts (double x1, double y1, double xm, double ym, double x2, double y2, int arc)
         {
            void translate (double *xp, double *yp)
            {
               if (a)
                  warnx ("TODO rotate footprint");
               (*xp) += dx;
               (*yp) += dy;
            }
            translate (&x1, &y1);
            translate (&x2, &y2);
            translate (&xm, &ym);
            void edges (double x, double y)
            {                   // Record limits
               if (x < lx)
                  lx = x;
               if (x > hx)
                  hx = x;
               if (y < ly)
                  ly = y;
               if (y > hy)
                  hy = y;
            }
            cuts = realloc (cuts, (cutn + 1) * sizeof (*cuts));
            if (!cuts)
               errx (1, "malloc");
            cuts[cutn].used = 0;
            cuts[cutn].x1 = x1;
            cuts[cutn].y1 = y1;
            edges (x1, y1);
            cuts[cutn].x2 = x2;
            cuts[cutn].y2 = y2;
            edges (x1, y1);
            if (arc)
            {
               cuts[cutn].xm = xm;
               cuts[cutn].ym = ym;
               edges (xm, ym);
            }
            cuts[cutn].arc = arc;
            cutn++;
         }
         pcb_t *o2 = pcb_find (o, "layer", NULL);
         if (!o2 || o2->valuen != 1 || !o2->values[0].istxt || strcmp (o2->values[0].txt, layer))
            return;
         if (!(o2 = pcb_find (o, "end", NULL)) || !o2->values[0].isnum || !o2->values[1].isnum)
            return;
         double x2 = o2->values[0].num,
            y2 = o2->values[1].num;
         if (!(o2 = pcb_find (o, "start", NULL)) || !o2->values[0].isnum || !o2->values[1].isnum)
         {
            if (!(o2 = pcb_find (o, "center", NULL)) || o2->valuen != 2 || !o2->values[0].isnum || !o2->values[1].isnum)
               return;          /* not a circle */
            long double cx = o2->values[0].num,
               cy = o2->values[1].num;
            long double r = sqrtl ((cx - x2) * (cx - x2) + (cy - y2) * (cy - y2));
            makecuts (cx - r, cy, cx, cy + r, cx + r, cy, 1);
            makecuts (cx + r, cy, cx, cy - r, cx - r, cy, 1);
            return;
         }
         double x1 = o2->values[0].num,
            y1 = o2->values[1].num;
         double xm = 0,
            ym = 0;
         char arc = 0;
         if ((o2 = pcb_find (o, "mid", NULL)) && o2->values[0].isnum && o2->values[1].isnum)
         {
            arc = 1;
            xm = o2->values[0].num;
            ym = o2->values[1].num;
         }
         makecuts (x1, y1, xm, ym, x2, y2, arc);
      }
      o = NULL;
      while ((o = pcb_find (pcb, "gr_line", o)))
         add (o, 0, 0, 0);
      while ((o = pcb_find (pcb, "gr_arc", o)))
         add (o, 0, 0, 0);
      while ((o = pcb_find (pcb, "gr_circle", o)))
         add (o, 0, 0, 0);
      pcb_t *fp = NULL;
      while ((fp = pcb_find (pcb, "footprint", fp)))
      {
         o2 = pcb_find (fp, "at", NULL);
         if (!o2 || o2->valuen < 2 || !o2->values[0].isnum || !o2->values[1].isnum)
            continue;
         long double x = o2->values[0].num,
            y = o2->values[1].num,
            a = 0;
         if (o2->valuen >= 3 && o2->values[2].isnum)
            a = o2->values[2].num;
         while ((o = pcb_find (fp, "fp_line", o)))
            add (o, x, y, a);
         while ((o = pcb_find (fp, "fp_arc", o)))
            add (o, x, y, a);
         while ((o = pcb_find (fp, "fp_circle", o)))
            add (o, x, y, a);
      }
      ry = hy;
      char *points = NULL;
      size_t lpo;
      FILE *po = open_memstream (&points, &lpo);
      char *paths = NULL;
      size_t lpa;
      FILE *pa = open_memstream (&paths, &lpa);
      char started = 0;
      double *pointx = NULL;
      double *pointy = NULL;
      int pointn = 0,
         pointa = 0;
      int addpoint (double x, double y)
      {
         int p;
         for (p = 0; p < pointn && (pointx[p] != x || pointy[p] != y); p++);
         if (p == pointn)
         {
            if (p == pointa)
            {
               pointa += 100;
               pointx = realloc (pointx, sizeof (*pointx) * pointa);
               pointy = realloc (pointy, sizeof (*pointy) * pointa);
            }
            pointx[p] = x;
            pointy[p] = y;
            fprintf (po, "[%lf,%lf],", x, y);
            pointn++;
         }
         return p;
      }
      if (cutn)
      {                         /* Edge cut */
         double x = NAN,
            y = NAN;
         int start = -1;
         int todo = cutn;
         while (todo--)
         {
            int n,
              b1 = -1,
               b2 = -1;
            double d1 = 0,
               d2 = 0,
               t = 0,
               x1 = 0,
               y1 = 0,
               x2 = 0,
               y2 = 0;
            inline double dist (double x1, double y1)
            {
               return (x - x1) * (x - x1) + (y - y1) * (y - y1);
            }
            for (n = 0; n < cutn; n++)
               if (!cuts[n].used && ((t = dist (cuts[n].x1, cuts[n].y1)) < d1 || b1 < 0))
               {
                  b1 = n;
                  d1 = t;
               }
            for (n = 0; n < cutn; n++)
               if (!cuts[n].used && ((t = dist (cuts[n].x2, cuts[n].y2)) < d2 || b2 < 0))
               {
                  b2 = n;
                  d2 = t;
               }
            int b = 0;
            if (d1 < d2)
            {
               b = b1;
               x1 = cuts[b].x1;
               y1 = cuts[b].y1;
               x2 = cuts[b].x2;
               y2 = cuts[b].y2;
            } else
            {
               b = b2;
               x1 = cuts[b].x2;
               y1 = cuts[b].y2;
               x2 = cuts[b].x1;
               y2 = cuts[b].y1;
            }
            cuts[b].used = 1;
            if (!started || x1 != x || y1 != y)
            {
               if (start >= 0)
                  warnx ("Not closed path (%lf,%lf)", x1, y1);
               start = addpoint ((x = x1) - lx, ry - (y = y1));
               if (started)
                  fprintf (pa, "],");
               fprintf (pa, "[%d", start);
            }
            if (cuts[b].arc)
            {
               //warnx("Arc");
               //warnx("x1=%lf y1=%lf xm=%lf ym=%lf x2=%lf y2=%lf", x1, y1, xm, ym, x2, y2);
               double xm = cuts[b].xm;
               double ym = cuts[b].ym;
               double xq = (x1 + x2) / 2;
               double yq = (y1 + y2) / 2;
               double qm = sqrt ((xq - xm) * (xq - xm) + (yq - ym) * (yq - ym));
               double q2 = sqrt ((xq - x2) * (xq - x2) + (yq - y2) * (yq - y2));
               double as = atan2 (q2, qm);
               double r = q2 / cos (2 * as - M_PI / 2);
               //warnx("xq=%lf yq=%lf qm=%lf q2=%lf as=%lf r=%lf", xq, yq, qm, q2, as * 180.0 / M_PI, r);
               double cx = xm + (xq - xm) * r / qm;
               double cy = ym + (yq - ym) * r / qm;
               double a1 = atan2 (y1 - cy, x1 - cx);
               double am = atan2 (ym - cy, xm - cx);
               double a2 = atan2 (y2 - cy, x2 - cx);
               //warnx("cx=%lf cy=%lf a1=%lf am=%lf a2=%lf", cx, cy, a1 * 180.0 / M_PI, am * 180.0 / M_PI, a2 * 180.0 / M_PI);
               if (a2 <= a1 && (am > a1 || am < a2))
                  a2 += 2 * M_PI;
               else if (a2 >= a1 && (am < a1 || am > a2))
                  a2 -= 2 * M_PI;
               if (delta < r)
               {
                  double da = 2 * acos (1 - delta / r);
                  int steps = ((a2 - a1) / da + 1);
                  //warnx("da=%lf steps=%d", da * 180.0 / M_PI, steps);
                  if (steps < 0)
                     steps = -steps;
                  for (int i = 1; i < steps; i++)
                  {
                     double a = a1 + (a2 - a1) * i / steps;
                     int p = addpoint ((x = (cx + r * cos (a))) - lx, ry - (y = (cy + r * sin (a))));
                     if (p == start)
                        start = -1;
                     else
                        fprintf (pa, ",%d", p);
                  }
               }
            }
            started = 1;
            if (x2 != x || y2 != y)
            {
               int p = addpoint ((x = x2) - lx, ry - (y = y2));
               if (p == start)
                  start = -1;
               else
                  fprintf (pa, ",%d", p);
            }
         }
         if (started)
            fprintf (pa, "]");
      }
      fclose (po);
      if (lpo)
         points[--lpo] = 0;
      fclose (pa);
      if (tag)
         fprintf (f, "\nmodule %s(h=pcbthickness,r=0){linear_extrude(height=h)offset(r=r)polygon(points=[%s],paths=[%s]);}\n", tag,
                  points, paths);
      free (points);
      free (paths);
      free (pointx);
      free (pointy);
      free (cuts);
   }
   {
      char edgecuts[] = "Edge.Cuts";
      if (layerpcb > 0 && layerpcb < 10)
         sprintf (edgecuts, "User.%d", layerpcb);
      char casework[] = "Edge.Cuts";
      if (layercase > 0 && layercase < 10)
         sprintf (casework, "User.%d", layercase);
      else
         strcpy (casework, edgecuts);
      outline (edgecuts, NULL); // Gets min/max set for this - does not output
      outline (casework, "outline");    // Updates min/max before output
      outline (edgecuts, "pcb");        // Actually output this time
   }

   double edgewidth = 0,
      edgelength = 0;
   if (lx < DBL_MAX)
      edgewidth = hx - lx;
   if (ly < DBL_MAX)
      edgelength = hy - ly;
   if (!spacing)
      spacing = edgewidth + casewall * 2 + 10;
   if (!edgewidth || !edgelength)
      errx (1, "Specify pcb size");
   fprintf (f, "spacing=%lf;\n", spacing);
   fprintf (f, "pcbwidth=%lf;\n", edgewidth);
   fprintf (f, "pcblength=%lf;\n", edgelength);

   struct
   {
      char *filename;           // Filename (malloc)
      char *desc;               // Description (malloc)
      unsigned char n:1;        // If module expects n parameter
   } *modules = NULL;
   int modulen = 0;

   int find_module (const char *fn, const char *a, const char *b)
   {                            // Find a module by filename, and return number, <0 for failed (including file not existing)
      int n;
      for (n = 0; n < modulen; n++)
         if (!strcmp (modules[n].filename, fn))
            break;
      if (n < modulen)
         return n;
      if (access (fn, R_OK))
         return -1;
      modules = realloc (modules, (++modulen) * sizeof (*modules));
      if (!modules)
         errx (1, "malloc");
      memset (modules + n, 0, sizeof (*modules));
      modules[n].filename = strdup (fn);
      if (a && !b)
         modules[n].desc = strdup (a);
      else if (!a && b)
         modules[n].desc = strdup (b);
      else if (asprintf (&modules[n].desc, "%s %s", a, b) < 0)
         errx (1, "malloc");
      if (debug)
         warnx ("New module %s %s %s", fn, a, b);
      return n;
   }
   int add_module (const char *fn, const char *a, const char *b, char **numberp)
   {                            // Check module with substitution for ℕ
      const char *f = fn;
      while (*f)
      {
         while (*f && !isdigit (*f))
            f++;
         if (!*f)
            break;
         char *new = NULL;
         size_t len = 0;
         FILE *o = open_memstream (&new, &len);
         const char *q = fn;
         while (q < f)
            fputc (*q++, o);
         fprintf (o, "ℕ");
         while (isdigit (*f))
            f++;
         if (*f == '.')
         {
            f++;
            while (isdigit (*f))
               f++;
         }
         if (numberp)
         {
            *numberp = NULL;
            size_t len;
            FILE *o = open_memstream (numberp, &len);
            while (q < f)
               fputc (*q++, o);
            fclose (o);
         }
         while (*q)
            fputc (*q++, o);
         fclose (o);
         int n = find_module (new, a, b);
         free (new);
         if (n >= 0)
         {
            modules[n].n = 1;
            return n;
         }
      }
      return find_module (fn, a, b);
   }

   const char *checkignore (const char *ref)
   {
      if (!ignore || !ref || !*ref || !*ignore)
         return NULL;
      const char *i = ignore;
      while (*i)
      {
         const char *r = ref;
         while (*i && *i != ',' && *r && *i == *r)
         {
            i++;
            r++;
         }
         if ((!*i || *i == ',') && !*r)
            return ref;
         while (*i && *i != ',')
            i++;
         while (*i == ',')
            i++;
      }
      return NULL;
   }

   /* The main PCB */
   for (int side = 0; side < 2; side++)
   {
      const char *sidename = side ? "bottom" : "top";
      int count = 0;
      fprintf (f, "// Parts to go on PCB (%s)\nmodule parts_%s(part=false,hole=false,block=false){\n", sidename, sidename);
      o = NULL;
      while ((o = pcb_find (pcb, "footprint", o)))
      {
         char back = 0;         /* back of board */
         if (!(o2 = pcb_find (o, "layer", NULL)) || o2->valuen != 1 || !o2->values[0].istxt)
            continue;
         if (!strcmp (o2->values[0].txt, "B.Cu"))
            back = 1;
         else if (strcmp (o2->values[0].txt, "F.Cu"))
            continue;
         if (side != back)
            continue;
         // Find part reference
         const char *ref = NULL;
         o2 = NULL;
         while ((o2 = pcb_find (o, "property", o2)))
         {
            if (o2->valuen >= 2 && o2->values[0].istxt && !strcmp (o2->values[0].txt, "Reference") && o2->values[1].istxt)
            {
               ref = o2->values[1].txt;
               break;
            }
         }
         if (checkignore (ref))
            continue;
         o2 = NULL;

         int n = -1;
         char *index = NULL;

         {                      // LCSC Part #
            const char *lcsc = NULL;
            o2 = NULL;
            while ((o2 = pcb_find (o, "property", o2)))
            {
               if (o2->valuen >= 2 && o2->values[0].istxt && !strcmp (o2->values[0].txt, "LCSC Part #") && o2->values[1].istxt)
               {
                  lcsc = o2->values[1].txt;
                  break;
               }
               if (lcsc)
                  n = find_module (lcsc, ref, NULL);    // No number expanding on this
            }
         }

         // Footprint
         const char *footprint = NULL;
         if (n < 0 && o->valuen >= 1 && o->values[0].istxt)
         {
            footprint = strchr (o->values[0].txt, ':');
            if (footprint)
               footprint++;
            else
               footprint = o->values[0].txt;
            n = add_module (footprint, ref, NULL, &index);
         }

         if (n >= 0)
         {                      // footprint level orientation
            if (debug && ref)
               warnx ("Module %s %s%s", ref, ref, back ? " (back)" : "");
            if ((o3 = pcb_find (o, "at", NULL)) && o3->valuen >= 2 && o3->values[0].isnum && o3->values[1].isnum)
            {
               fprintf (f, "translate([%lf,%lf,%lf])", o3->values[0].num - lx, ry - o3->values[1].num, back ? 0 : pcbthickness);
               if (o3->valuen >= 3 && o3->values[2].isnum)
                  fprintf (f, "rotate([0,0,%lf])", o3->values[2].num);
            }
            if (back)
               fprintf (f, "rotate([180,0,0])");
            if (modules[n].n && index)
               fprintf (f, "m%d(part,hole,block,case%s,%s); // %s%s\n", n, sidename, index, modules[n].desc, back ? "" : " (back)");
            else
               fprintf (f, "m%d(part,hole,block,case%s); // %s%s\n", n, sidename, modules[n].desc, back ? "" : " (back)");
            count++;
            continue;
         }
         free (index);

         // Add 3D models
         int id = 0;
         while ((o2 = pcb_find (o, "model", o2)))
         {
            if (o2->valuen < 1 || !o2->values[0].istxt)
               continue;        /* Not 3D model */
            id++;
            char *model = strdup (o2->values[0].txt);
            if (!model)
               errx (1, "malloc");
            char *leaf = strrchr (model, '/');
            if (leaf)
               leaf++;
            else
               leaf = model;
            char *e = strrchr (model, '.');
            if (e)
               *e = 0;
            int q = add_module (leaf, o->values[0].txt ? : ref ? : "-", leaf, &index);
            char *refn;
            if (asprintf (&refn, "%s.%d", ref, id) < 0)
               errx (1, "malloc");
            if (checkignore (refn))
            {
               free (refn);
               free (index);
               continue;
            }
            if (debug && ref)
               warnx ("Module %s.%d %s%s", ref, id, leaf, back ? " (back)" : "");
            free (refn);
            if (q >= 0)
            {                   // Model orientation
               n = q;
               if ((o3 = pcb_find (o, "at", NULL)) && o3->valuen >= 2 && o3->values[0].isnum && o3->values[1].isnum)
               {
                  fprintf (f, "translate([%lf,%lf,%lf])", o3->values[0].num - lx, ry - o3->values[1].num, back ? 0 : pcbthickness);
                  if (o3->valuen >= 3 && o3->values[2].isnum)
                     fprintf (f, "rotate([0,0,%lf])", o3->values[2].num);
               }
               if (back)
                  fprintf (f, "rotate([180,0,0])");
               if ((o3 = pcb_find (o2, "offset", NULL)) && (o3 = pcb_find (o3, "xyz", NULL)) && o3->valuen >= 3
                   && o3->values[0].isnum && o3->values[1].isnum && o3->values[2].isnum && (o3->values[0].num || o3->values[1].num
                                                                                            || o3->values[2].num))
                  fprintf (f, "translate([%lf,%lf,%lf])", o3->values[0].num, o3->values[1].num, o3->values[2].num);
               if ((o3 = pcb_find (o2, "scale", NULL)) && (o3 = pcb_find (o3, "xyz", NULL)) && o3->valuen >= 3
                   && o3->values[0].isnum && o3->values[1].isnum && o3->values[2].isnum && (o3->values[0].num != 1
                                                                                            || o3->values[1].num != 1
                                                                                            || o3->values[2].num != 1))
                  fprintf (f, "scale([%lf,%lf,%lf])", o3->values[0].num, o3->values[1].num, o3->values[2].num);
               if ((o3 = pcb_find (o2, "rotate", NULL)) && (o3 = pcb_find (o3, "xyz", NULL)) && o3->valuen >= 3
                   && o3->values[0].isnum && o3->values[1].isnum && o3->values[2].isnum && (o3->values[0].num || o3->values[1].num
                                                                                            || o3->values[2].num))
                  fprintf (f, "rotate([%lf,%lf,%lf])", -o3->values[0].num, -o3->values[1].num, -o3->values[2].num);
               if (modules[n].n && index)
                  fprintf (f, "m%d(part,hole,block,case%s,%s); // %s%s\n", n, sidename, index, modules[n].desc,
                           back ? "" : " (back)");
               else
                  fprintf (f, "m%d(part,hole,block,case%s); // %s%s\n", n, sidename, modules[n].desc, back ? "" : " (back)");
               if (n < 0)
                  warnx ("Missing part %s %s", ref, footprint);
            } else
            {
               fprintf (f, "// Missing model %s.%d %s%s\n", ref, id, leaf, back ? " (back)" : "");
               warnx ("Missing %s.%d %s%s %s", ref, id, leaf, back ? " (back)" : "", footprint);
            }
            free (model);
            free (index);
         }
      }
      fprintf (f, "}\n\n");
      fprintf (f, "parts_%s=%d;\n", sidename, count);
   }

   fprintf (f, "module b(cx,cy,z,w,l,h){translate([cx-w/2,cy-l/2,z])cube([w,l,h]);}\n");

   /* Used models */
   for (int n = 0; n < modulen; n++)
   {
      if (modules[n].n)
         fprintf (f, "module m%d(part=false,hole=false,block=false,height,N=0)\n{ // %s\n", n, modules[n].desc);
      else
         fprintf (f, "module m%d(part=false,hole=false,block=false,height)\n{ // %s\n", n, modules[n].desc);
      copy_file (f, modules[n].filename);
      fprintf (f, "}\n\n");
   }

   /* Final SCAD */
   copy_file (f, "../case.scad");

   if (debug)
      fprintf (f, "translate([spacing*2,0,0])preview();\n");
   if (!norender)
      fprintf (f, "bottom(); translate([spacing,0,0])top();\n");

   if (f != stdout)
      fclose (f);
}

int
main (int argc, const char *argv[])
{
   poptContext optCon;          /* context for parsing  command - line options */
   {
      const struct poptOption optionsTable[] = {
         {"pcb-file", 'i', POPT_ARG_STRING, &pcbfile, 0, "PCB file", "filename"},
         {"scad-file", 'o', POPT_ARG_STRING, &scadfile, 0, "Openscad file", "filename"},
         {"ignore", 'I', POPT_ARG_STRING, &ignore, 0, "Ignore", "ref{,ref}"},
         {"bottom", 'b', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &casebottom, 0, "Case bottom", "mm"},
         {"top", 't', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &casetop, 0, "Case top", "mm"},
         {"wall", 'w', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &casewall, 0, "Case wall", "mm"},
         {"edge", 'e', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &edge, 0, "Case edge", "mm"},
         {"fit", 'f', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &fit, 0, "Case fit", "mm"},
         {"hull-cap", 3, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &hullcap, 0, "Hull cap", "mm"},
         {"hull-edge", 3, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &hulledge, 0, "Hull edge", "mm"},
         {"no-hull", 'h', POPT_ARG_NONE, &nohull, 0, "No hull on parts"},
         {"margin", 'm', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &margin, 0, "margin", "mm"},
         {"lip", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &lip, 0, "lip offset", "mm"},
         {"pcb", 0, POPT_ARG_INT, &layerpcb, 0, "Use User.N as PCB border instead of Edge.Cuts", "N"},
         {"case", 0, POPT_ARG_INT, &layercase, 0, "Use User.N as case border instead of pcb", "N"},
         {"pcb-thickness", 'T', POPT_ARG_DOUBLE, &pcbthickness, 0, "PCB thickness (default: auto)", "mm"},
         {"model-dir", 'M', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &modeldir, 0, "Model directory", "dir"},
         {"spacing", 's', POPT_ARG_DOUBLE, &spacing, 0, "Spacing (default: auto)", "mm"},
         {"curve-delta", 'D', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &delta, 0, "Curve delta", "mm"},
         {"no-render", 'n', POPT_ARG_NONE, &norender, 0, "No-render, just define base() and top()"},
         {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug"},
         POPT_AUTOHELP {}
      };

      optCon = poptGetContext (NULL, argc, argv, optionsTable, 0);
      /* poptSetOtherOptionHelp(optCon, ""); */

      int c;
      if ((c = poptGetNextOpt (optCon)) < -1)
         errx (1, "%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));

      if (poptPeekArg (optCon) && !pcbfile)
         pcbfile = poptGetArg (optCon);

      if (poptPeekArg (optCon) || !pcbfile)
      {
         poptPrintUsage (optCon, stderr, 0);
         return -1;
      }
   }
   if (!scadfile)
   {
      const char *f = strrchr (pcbfile, '/');
      if (f)
         f++;
      else
         f = pcbfile;
      const char *e = strrchr (f, '.');
      if (!e || !strcmp (e, ".scad"))
         e = f + strlen (f);
      if (asprintf (&scadfile, "%.*s.scad", (int) (e - pcbfile), pcbfile) < 0)
         errx (1, "malloc");
   }
   pcb_t *pcb = pcb_load (pcbfile);
   if (strcmp (pcb->tag, "kicad_pcb"))
      errx (1, "Not a kicad_pcb (%s)", pcb->tag);
   write_scad (pcb);
   pcb = pcb_free (pcb);

   poptFreeContext (optCon);
   return 0;
}
