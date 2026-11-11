/* a3d_int.h - the renderer's internals shared by its C modules. */
#ifndef A3D_INT_H
#define A3D_INT_H
#include "a8hw.h"
#include "a3d.h"

extern uint8_t a3d_bsel;
#pragma zpsym ("a3d_bsel")
extern uint8_t a3d_ylo[96], a3d_yhi[96];
extern uint8_t a3d_pxlo[256], a3d_pxhi[256];
extern int16_t a3d_vcx, a3d_vcy;
extern uint8_t a3d_fn;
/* asm: gather a3d_fn vertices of idx into a3d_px/py and test the winding:
   0 = back-facing, 1 = front (gathered), 2 = a vertex is behind the near
   plane (C clips) */
extern uint8_t __fastcall__ a3d_gather_(const uint8_t *idx);
extern uint8_t __fastcall__ a3d_face_(const uint8_t *idx);
extern uint8_t a3d_front_(void);           /* winding of a3d_px/py 0, 1, 2 */
extern const a3d_mat *a3d_mma;
extern a3d_mat *a3d_mmo;
extern void __fastcall__ a3d_mat_mul_(const a3d_mat *b);
extern int16_t a3d_rv[3], a3d_rout[3];
extern void __fastcall__ a3d_rot16_(const a3d_mat *m);
extern void a3d_math_init(void);
extern void a3d_hud_dli(void);
/* near-clipped gather into a3d_px/py (C); returns the count */
extern uint8_t a3d_gather_near(uint8_t n, const uint8_t *idx);
/* the point where edge a (in front) - b (behind) meets the near plane,
   projected into a3d_sx/sy[A3D_TMP] */
#define A3D_TMP (A3D_MAXV - 1)
extern void a3d_near(uint8_t a, uint8_t b);
#endif
