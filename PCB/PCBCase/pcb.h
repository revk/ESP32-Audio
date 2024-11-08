// KiCad PCB file handling

typedef struct pcb_s pcb_t;
typedef struct pcb_val_s pcb_val_t;

struct pcb_val_s
{                               /* value */
   /* only one set */
   unsigned char isobj:1;       /* object */
   unsigned char isnum:1;       /* number */
   unsigned char isbool:1;      /* boolean */
   unsigned char istxt:1;       /* text */
   unsigned char islit:1;       /* literal */
   union
   {                            /* the value */
      pcb_t *obj;
      double num;
      const char *txt;
      unsigned char istrue:1;
   };
};

struct pcb_s
{                               /* an object */
   const char *tag;             /* object tag */
   int valuen;                  /* number of values */
   pcb_val_t *values;           /* the values */
};

pcb_t *pcb_load (const char *pcbfile);  // Load a PCB from file
void pcb_write (const char *pcbfile, pcb_t *);  // Write a PCB to file
pcb_t *pcb_find (pcb_t * pcb, const char *tag, pcb_t * prev);   // Find next object by tag
void pcb_clear (pcb_t *);       // Clear an object of values
void pcb_delete (pcb_t *);      // Delete an object
pcb_t *pcb_free (pcb_t *);      // Delete all (including strings);
pcb_val_t *pcb_append (pcb_t *);        // Append a value to an object
pcb_val_t *pcb_append_num (pcb_t * o, double val);      // Append a number
pcb_val_t *pcb_append_lit (pcb_t * o, const char *);    // Append a literal
pcb_val_t *pcb_append_txt (pcb_t * o, const char *);    // Append a txt
pcb_val_t *pcb_append_bool (pcb_t * o, int);    // Append a bool
pcb_t *pcb_append_obj (pcb_t * o, const char *);        // Append an obj
