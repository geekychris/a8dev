#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>
#define VROWS 56                          /* the view: ANTIC D rows (2 scan lines) */
#define HOR   14                          /* the horizon's row on flat road */
#define VIEW_LINE0 35                     /* the scan line of view row 0 */
void render_init(void);
void render_frame(void);
uint8_t *render_dl(void);
uint8_t *render_hud(void);
uint8_t *render_msg(void);
extern uint16_t cam_z;
extern int16_t cam_y, cam_x, cam_dir16, cam_d;
#endif
