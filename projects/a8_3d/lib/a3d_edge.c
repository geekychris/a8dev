/* a3d_edge.c - edges and outlines of transformed vertices (near-clipped),
   apart from a3d_line.c so a game that only draws lines doesn't link the
   transform. */
#include <string.h>
#include "a3d_int.h"

void a3d_edge(uint8_t a, uint8_t b, uint8_t c)
{
    if (a3d_vclip[a]) {
        if (a3d_vclip[b]) return;
        a3d_near(b, a);
        a = A3D_TMP;
    } else if (a3d_vclip[b]) {
        a3d_near(a, b);
        b = A3D_TMP;
    }
    a3d_line(a3d_sx[a], a3d_sy[a], a3d_sx[b], a3d_sy[b], c);
}

uint8_t a3d_face_wire(uint8_t n, const uint8_t *idx, uint8_t c)
{
    static int16_t wx[A3D_MAXP], wy[A3D_MAXP];
    uint8_t i, r;
    a3d_fn = n;
    r = a3d_gather_(idx);
    if (r == 2) {
        n = a3d_gather_near(n, idx);
        r = n >= 3 && a3d_front_();
    }
    if (!r) return 0;
    memcpy(wx, a3d_px, n * 2);
    memcpy(wy, a3d_py, n * 2);
    for (i = 0; i < n; ++i)
        a3d_line(wx[i], wy[i], wx[i + 1 == n ? 0 : i + 1], wy[i + 1 == n ? 0 : i + 1], c);
    return 1;
}
