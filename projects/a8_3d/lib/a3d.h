/*
 * a3d.h - a small 3D renderer for the Atari 8-bit (6502 @ 1.77 MHz).
 *
 * Display: ANTIC mode D, 160 x up to 96 pixels in 4 colours, double
 * buffered by switching between two display lists, plus up to 3 rows of
 * ANTIC 2 text (the HUD, shared by both lists). Everything is inside the
 * fixed screen area $A000-$BFFF of sdk/cfg/a8.cfg, so it runs on a 64K
 * 800XL (and a 48K 400/800); no 130XE bank is needed.
 *
 *   $A010-$AF0F  buffer 0 (96 lines x 40 bytes)   $AF10 display list 0
 *   $B010-$BF0F  buffer 1                         $BF10 display list 1
 *   $AF80-$AFF7  text rows (3 x 40)
 *
 * Buffer 1 is buffer 0 + $1000, so the drawing code finds a line of the
 * back buffer with one ORA of the line address's high byte (a3d_bsel).
 *
 * Maths (asm, a3d_math.s): signed 8 x 8 multiplies by quarter squares
 * (2 KB of tables), a Q7 rotation matrix (int8, 127 = 1.0), model vertices
 * as int8 triples rotated by the matrix and added to an int16 origin in
 * camera space (x right, y down, z forward), perspective through a
 * reciprocal table (focal length * 256 / z, z normalised to 128..255).
 *
 * Drawing (asm, a3d_poly.s polygons, a3d_span.s spans/clears/lines): convex polygons clockwise on the
 * screen (anticlockwise ones draw nothing: free back-face culling), filled
 * a span at a time with whole bytes in the middle, optionally in two
 * alternating patterns (dither: 7 shades from 4 colours); clipped lines;
 * fast clears of rows of the back buffer.
 *
 * C: set-up (a3d_init.c), matrices (a3d_mat.c), near-plane clipping of
 * faces and screen clipping of polygons crossing the top or bottom
 * (a3d_clip.c), clipped lines (a3d_line.c), text (a3d_text.c). Built as an
 * ar65 library: a game links only the modules it calls.
 */
#ifndef A3D_H
#define A3D_H
#include <stdint.h>

#define A3D_W       160
#define A3D_MAXH    96
#define A3D_MAXV    64          /* transformed vertices */
#define A3D_MAXP    16          /* vertices of one polygon (after clipping) */
#define A3D_NEAR    8           /* near plane, camera units */
#define A3D_TEXT    ((uint8_t *)0xAF80)

/* ---------------------------------------------------------------- display */
/* lines: 3D lines (2 scan lines each, <= 96); text_rows: 0..3 ANTIC 2 rows
   below them (A3D_TEXT, shared by both buffers) */
extern void a3d_init(uint8_t lines, uint8_t text_rows);
extern uint8_t a3d_h;            /* lines of 3D */
/* 1 before a3d_init: every line shown twice (4 scan lines, <= 48 lines):
   half the lines to fill. The display lists move to $A800 / $B800. */
extern uint8_t a3d_double;
extern uint8_t a3d_back;         /* 0/1: the buffer being drawn */
/* show the back buffer from the next frame, wait for that vertical blank
   (frame_wait(): sound ticks), return the frames since the previous flip */
extern uint8_t a3d_flip(void);
extern void a3d_show(void);      /* just switch (no wait) ... */
extern uint8_t a3d_sync(void);   /* ... then wait for a VBI after it before drawing again */
extern uint8_t *a3d_dlist(uint8_t buf);   /* display list of a buffer */
/* text in the rows (ASCII, bit 7 = inverse); a number right-aligned in width */
extern void a3d_print(uint8_t x, uint8_t row, const char *s);
extern void a3d_num(uint8_t x, uint8_t row, uint16_t v, uint8_t width);
/* text rows: a DLI on the last 3D line sets their colours (luminance c1 on
   background c2), so COLOR0..COLOR2 stay free for the 3D. A game with its
   own DLIs replaces VDSLST and chains to a3d_hud_dli on that line. */
extern uint8_t a3d_hud_c1, a3d_hud_c2;
extern void a3d_hud_dli(void);
/* set flags (0x80 = DLI) on 3D line y's display list instruction, both lists */
extern void a3d_dl_flag(uint8_t y, uint8_t flags);

/* fill patterns for colour 0..3 */
#define A3D_C0  0x00
#define A3D_C1  0x55
#define A3D_C2  0xAA
#define A3D_C3  0xFF
/* clear 3D lines y0..y1-1 of the back buffer to a pattern byte */
extern uint8_t a3d_cy0, a3d_cy1;
extern void __fastcall__ a3d_clear_rows_(uint8_t pattern);
#define a3d_clear_rows(y0, y1, pat) (a3d_cy0 = (y0), a3d_cy1 = (y1), a3d_clear_rows_(pat))
#define a3d_clear(pat) a3d_clear_rows(0, a3d_h, pat)
/* clear the rectangle drawn into the back buffer since it was last cleared
   (polygons, spans and lines record it): cost follows what was drawn */
extern void __fastcall__ a3d_clear_dirty_(uint8_t pattern);
#define a3d_clear_dirty(pat) a3d_clear_dirty_(pat)

/* ---------------------------------------------------------------- drawing */
/* pattern bytes used on even and odd lines by polygons and spans */
extern uint8_t a3d_pat_e, a3d_pat_o;
#define a3d_colour(c)      (a3d_pat_e = a3d_pat_o = (uint8_t)((c) * 0x55))
#define a3d_pattern(e, o)  (a3d_pat_e = (e), a3d_pat_o = (o))
extern const uint8_t a3d_shade[7][2];    /* 7 shades: 0, 0/1 dither, 1, 1/2, 2, 2/3, 3 */
#define a3d_shade_set(s)   (a3d_pat_e = a3d_shade[s][0], a3d_pat_o = a3d_shade[s][1])

/* a polygon on the screen: a3d_px/a3d_py, n vertices, clockwise; clipped */
extern int16_t a3d_px[A3D_MAXP], a3d_py[A3D_MAXP];
extern void __fastcall__ a3d_poly(uint8_t n);
/* the same, coordinates already inside 0..160 x 0..a3d_h (no clipping) */
extern void __fastcall__ a3d_fill(uint8_t n);
/* one span, x0 <= x < x1, on line y (no clipping) */
extern uint8_t a3d_sx0, a3d_sx1;
extern void __fastcall__ a3d_span_(uint8_t y);
#define a3d_span(y, x0, x1) (a3d_sx0 = (x0), a3d_sx1 = (x1), a3d_span_(y))

/* a line in colour c (0..3), clipped to the 3D area */
extern void a3d_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c);
/* unclipped: coordinates inside 0..159 x 0..a3d_h-1 (a3d_lx0... globals) */
extern uint8_t a3d_lx0, a3d_ly0, a3d_lx1, a3d_ly1;
extern void __fastcall__ a3d_line_(uint8_t c);
extern void a3d_plot(uint8_t x, uint8_t y, uint8_t c);

/* ---------------------------------------------------------------- maths */
extern const int8_t a3d_sin8[320];       /* Q7 sine, 256 steps a turn; cos = +64 */
#define a3d_sin(a)  (a3d_sin8[(uint8_t)(a)])
#define a3d_cos(a)  (a3d_sin8[(uint8_t)(a) + 64])
/* a * b, signed 8 x 8 (asm) */
extern int8_t a3d_ma;
extern int16_t __fastcall__ a3d_mul8s_(int8_t b);
#define a3d_mul8s(a, b)  (a3d_ma = (a), a3d_mul8s_(b))
/* (a * b) >> 8, signed 16 x 16 (asm; the result is clamped to +-16383) */
extern int16_t a3d_mb;
extern int16_t __fastcall__ a3d_mulf_(int16_t a);
#define a3d_mulf(a, b)   (a3d_mb = (b), a3d_mulf_(a))
/* (a * b) / c without overflow for |a*b| < 2^31, in C (slow; clipping) */
extern int16_t a3d_muldiv(int16_t a, int16_t b, int16_t c);

/* rotation: m[row*3+col], rows = the camera's x, y, z axes in world space */
typedef struct { int8_t m[9]; } a3d_mat;
/* yaw about y (0 = looking along +z, 64 = along +x), then pitch about x
   (> 0 looks down), then roll about z */
extern void a3d_mat_ypr(a3d_mat *out, uint8_t yaw, uint8_t pitch, uint8_t roll);
extern void a3d_mat_mul(a3d_mat *out, const a3d_mat *a, const a3d_mat *b);   /* a * b */
extern void a3d_mat_ident(a3d_mat *out);
/* rotate a 16-bit vector by m (out = m * v): positions relative to the camera */
extern void a3d_rot16(const a3d_mat *m, int16_t x, int16_t y, int16_t z, int16_t *out);

/* the projection: centre (cx, cy) and focal length f in pixels: sx = cx +
   x*f/z, sy = cy - y*f/z (camera y is up). One table serves both axes; mode
   D pixels are ~1.17 times taller than wide, so a camera matrix from
   a3d_cam_ypr scales its y row by a3d_aspect (Q7, default 109 = 0.85).
   Camera-space y you compute yourself needs the same scale. */
extern void a3d_view(uint8_t cx, uint8_t cy, uint8_t f);   /* call before a3d_xform */
extern uint8_t a3d_aspect;
extern void a3d_cam_ypr(a3d_mat *out, uint8_t yaw, uint8_t pitch, uint8_t roll);

/* transformed vertices: camera space and screen */
extern int16_t a3d_vx[A3D_MAXV], a3d_vy[A3D_MAXV], a3d_vz[A3D_MAXV];
extern int16_t a3d_sx[A3D_MAXV], a3d_sy[A3D_MAXV];
extern uint8_t a3d_vclip[A3D_MAXV];      /* 1 = behind the near plane */
/* object set-up for a3d_xform: rotation, camera-space origin, scale shift
   (vertex units = 2^shift camera units, shift 0..7) */
extern a3d_mat a3d_om;
extern int16_t a3d_ox, a3d_oy, a3d_oz;
extern uint8_t a3d_oshift;
/* transform n int8 vertices (x, y, z triples) into a3d_v*[first...] and
   project them into a3d_sx/sy (a3d_vclip set for those behind the near plane) */
extern uint8_t a3d_xfirst, a3d_xn;
extern void __fastcall__ a3d_xform_(const int8_t *verts);
#define a3d_xform(first, n, verts) (a3d_xfirst = (first), a3d_xn = (n), a3d_xform_(verts))
/* project vertices first..first+n-1 from a3d_v* (after writing them yourself) */
extern void __fastcall__ a3d_project_(uint8_t n);
#define a3d_project(first, n) (a3d_xfirst = (first), a3d_project_(n))

/* a face through transformed vertices idx[0..n-1] (clockwise seen from the
   front): near-clipped, back-face culled, screen-clipped and filled with the
   current pattern. Returns 1 if anything was drawn. */
extern uint8_t a3d_face(uint8_t n, const uint8_t *idx);
/* the same as an outline in colour c */
extern uint8_t a3d_face_wire(uint8_t n, const uint8_t *idx, uint8_t c);
/* an edge between two transformed vertices (near-clipped), colour c */
extern void a3d_edge(uint8_t a, uint8_t b, uint8_t c);

/* ---------------------------------------------------------------- timing */
/* a timer in units of 2 scan lines (VCOUNT: 228 CPU cycles less DMA; PAL
   156 a frame, NTSC 131); a3d_since(t0) = ticks since t0 (< 256 frames) */
extern uint16_t a3d_ticks(void);
extern uint16_t a3d_since(uint16_t t0);
#define A3D_TICK_CYCLES 228

#endif
