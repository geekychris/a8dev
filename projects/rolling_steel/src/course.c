/* course.c - a course from the banks into memory (tools/gen_course.py made
   the packed data): its numbers (meta), the grid the physics rolls on (at
   GRID, with a pointer to each row), the picture (fonts, map, each row's
   colours and font, at FONTS). The packed grid is unpacked into the
   picture's place first and decoded from there. */
#include <string.h>
#include "rs.h"

#pragma static-locals (on)
#pragma code-name (push, "CODE2")   /* loaded low (rolling_steel.cfg) */

uint8_t c_w, c_rows, c_nf;
uint8_t *c_map, *c_cols, *c_rowfont;
uint16_t g_nx, g_nz;
#pragma bss-name (push, "LOWRAM")
uint8_t *g_row[360];                    /* each grid row's runs */
#pragma bss-name (pop)
PathPt *path;
uint8_t *boxes;
EnemySpec *enemies;

void grid_decode(void);                 /* gdec.s */

uint8_t banks_ok(void)
{
    uint8_t k;
    for (k = 0; k < 4; ++k)
        if (bank_peek(k, (const uint8_t *)0x7FFC) != 'R' || bank_peek(k, (const uint8_t *)0x7FFD) != 'S' ||
            bank_peek(k, (const uint8_t *)0x7FFE) != (uint8_t)('0' + k))
            return 0;
    return 1;
}

uint8_t course_load(uint8_t ci)
{
    const CourseSrc *s = &course_src[ci];
    uint8_t *p;
    uint8_t k;
    if (!banks_ok()) return 0;
    unlz(s->meta_bank, (const uint8_t *)s->meta_addr, FONTS);
    memcpy(meta, FONTS, META_MAX);
    path = (PathPt *)(meta + sizeof(MetaHdr));
    p = (uint8_t *)(path + MH->npath);
    boxes = p;
    for (k = 0; k < MH->nbox; ++k) p += sizeof(BoxHdr) + 4 * ((BoxHdr *)p)->nrows;
    enemies = (EnemySpec *)p;
    unlz(s->grid_bank, (const uint8_t *)s->grid_addr, FONTS);
    grid_decode();
    unlz(s->pic_bank, (const uint8_t *)s->pic_addr, FONTS);
    c_w = s->w;
    c_rows = s->rows;
    c_nf = s->nfonts;
    c_map = FONTS + (uint16_t)c_nf * 1024;
    c_cols = c_map + (uint16_t)c_rows * c_w;
    c_rowfont = c_cols + (uint16_t)c_rows * 4;
    return 1;
}
#pragma code-name (pop)
