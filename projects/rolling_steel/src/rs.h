/* Rolling Steel for the Atari 130XE: the shared declarations.

   Units (the "Atari edition"'s, src/phys.c): positions in Q8 units of the
   original's world (256 = one unit) from the course grid's corner, gx and
   gz across, gy (the ball's centre) up; velocities in Q12 units per 1/50 s
   step (4096 = one unit a step); the clock in steps. */
#ifndef RS_H
#define RS_H
#include <stdint.h>

#define NCOURSE   6
#define FONTS     ((uint8_t *)0x8000)   /* the course's fonts, map, row colours (rolling_steel.cfg) */
#define GRID      ((uint8_t *)0xD800)   /* the course's grid */
#define VIS_ROWS  23                    /* course rows on screen */
#define META_MAX  2048
#define NONE_H    (-32768)

typedef struct {
    uint8_t pic_bank;                   /* the packed picture: fonts, map, row colours and fonts */
    uint16_t pic_addr;
    uint8_t grid_bank;                  /* the packed grid */
    uint16_t grid_addr;
    uint8_t meta_bank;                  /* the packed numbers (Meta) */
    uint16_t meta_addr;
    uint8_t w, rows, nfonts;            /* the map's width and rows, its fonts */
    const char *name;
} CourseSrc;
extern const CourseSrc course_src[NCOURSE];

/* tools/gen_course.py's meta(): the course's numbers */
typedef struct {
    uint16_t px0;                       /* picture pixel = (gx + px0) >> 6 */
    int16_t ly0;                        /* picture line = ly0 - ((gy 210 + gz 147) >> 13) */
    uint16_t sx; int16_t sy; uint16_t sz;   /* the spawn */
    int16_t kill_y;
    uint16_t time, gold, silver, bronze;    /* steps */
    uint8_t npath, nbox, nenemy, pad;
} MetaHdr;
typedef struct { uint16_t x, z; int16_t y; } PathPt;          /* y: bit 0 = safe to come back to */
typedef struct { uint8_t kind, nrows; uint16_t k0; int16_t y0, y1; int8_t dvx, dvz; } BoxHdr;   /* + nrows x (gx0, gx1) */
enum { BOX_ACID, BOX_GOAL, BOX_ZONE };
typedef struct { uint8_t kind, pad; uint16_t speed, x; int16_t y; uint16_t z, range; int16_t ax, az; } EnemySpec;

#define meta      ((uint8_t *)0x0400) /* $0400-$0BFF: free with the OS off */
#define MH ((MetaHdr *)meta)
extern PathPt *path;
extern uint8_t *boxes;
extern EnemySpec *enemies;

/* the course in memory (course.c) */
extern uint8_t c_w, c_rows, c_nf;
extern uint8_t *c_map;                  /* rows x w characters */
extern uint8_t *c_cols;                 /* rows x 4: COLBK, PF0, PF1, PF2 */
extern uint8_t *c_rowfont;              /* rows: the font (0..nf-1) */
extern uint16_t g_nx, g_nz;
extern uint8_t *g_row[];
uint8_t course_load(uint8_t ci);        /* 0 if a bank is missing */
uint8_t banks_ok(void);

/* unlz.s */
extern void __fastcall__ unlz(uint8_t bank, const uint8_t *src, uint8_t *dst);
extern uint8_t __fastcall__ bank_peek(uint8_t bank, const uint8_t *addr);

/* disp.s: the course display */
extern uint16_t view_x, view_y;         /* the view's top left in the picture: pixels, lines */
void disp_init(void);
void disp_course(void);                 /* the loaded course's tables to the display */
extern uint8_t hud[80];                 /* the two text rows */

/* os.s */
extern uint8_t take_latch(void);
extern uint8_t in_held;
void os_off(void);

/* maths.s */
int16_t __fastcall__ pic_line(int16_t gy, uint16_t gz);   /* uses MH->ly0 */
extern uint16_t cq_i, cq_k, gq_x, gq_z;
#pragma zpsym ("cq_i")
#pragma zpsym ("cq_k")
#pragma zpsym ("gq_x")
#pragma zpsym ("gq_z")
uint8_t cell_q(void);                   /* 1 if the cell (cq_i, cq_k) is deck: cell_h, cell_c */
uint8_t ground_q(void);                 /* the deck under (gq_x, gq_z): g_h, g_sx, g_sz, g_cls */
extern int16_t cell_h;
extern uint8_t cell_c;

/* phys.c: balls 0 the marble, 1.. the enemies */
#define NB 4
extern uint16_t b_x[NB], b_z[NB];
extern int16_t b_y[NB];
extern uint8_t b_xf[NB], b_zf[NB], b_yf[NB];
extern int16_t b_vx[NB], b_vz[NB], b_vy[NB];
extern uint8_t b_gr[NB], b_cls[NB], b_live[NB];
extern uint8_t nen, en_kind[NB];
extern uint16_t lg_x, lg_z;             /* where the marble last had the ground */
extern int16_t lg_y;
extern uint8_t ph_event;                /* PE_* bits this step */
enum { PE_DISSOLVED = 1, PE_EATEN = 2, PE_FELL = 4, PE_GOAL = 8, PE_CLACK = 16, PE_LAND = 32, PE_WALL = 64 };
void phys_reset(void);                  /* a course was loaded */
void phys_enemies_home(void);
void phys_place(uint16_t x, int16_t y, uint16_t z);   /* the marble, at rest */
void phys_step(int8_t jx, int8_t jz);   /* one 1/50 s step; jx, jz -1..1 (the stick) */
uint8_t ground(uint16_t gx, uint16_t gz);   /* 1 if there's deck: g_h, g_sx, g_sz, g_cls */
extern int16_t g_h, g_sx, g_sz;
extern uint8_t g_cls;
uint16_t __fastcall__ speed_of(int16_t vx, int16_t vz);   /* maths.s */
#endif
