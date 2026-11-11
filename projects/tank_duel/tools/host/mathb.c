/* host stand-in for src/mathb.s (tools/host_check.sh): the same sums */
#include <stdint.h>
int16_t M_a, M_b;
uint8_t M_d;
extern const int16_t sin8[256];
static const uint16_t atan_tab[65] = {
    0, 163, 326, 489, 651, 813, 975, 1136, 1297, 1457, 1617, 1775, 1933, 2090, 2246, 2401, 2555, 2708, 2860, 3010,
    3159, 3307, 3453, 3599, 3742, 3884, 4025, 4164, 4302, 4438, 4573, 4706, 4838, 4968, 5097, 5223, 5349, 5473, 5595,
    5715, 5834, 5952, 6068, 6182, 6295, 6406, 6516, 6624, 6731, 6837, 6941, 7043, 7144, 7244, 7342, 7439, 7534, 7628,
    7721, 7813, 7903, 7991, 8079, 8165, 8192
};
static uint16_t ratio(uint8_t mn, uint8_t mx) { return atan_tab[(mn * 64) / mx]; }
uint16_t angle_to_(void)
{
    uint16_t ax = M_a < 0 ? -M_a : M_a, az = M_b < 0 ? -M_b : M_b, a;
    if (!ax && !az) return 0;
    while ((ax | az) > 255) { ax >>= 1; az >>= 1; }
    a = ax <= az ? ratio(ax, az) : 16384 - ratio(az, ax);
    if (M_b < 0) a = 32768u - a;
    if (M_a < 0) a = -a;
    return a;
}
uint16_t odist_(void)
{
    uint16_t a = (uint16_t)(M_a < 0 ? -M_a : M_a) >> 4, b = (uint16_t)(M_b < 0 ? -M_b : M_b) >> 4, t;
    if (a < b) { t = a; a = b; b = t; }
    return a + (b >> 2) + (b >> 3);
}
int16_t along_(void) { return (int16_t)(M_d * sin8[(uint8_t)((uint16_t)M_a >> 8)]) >> 4; }
int16_t along8_(void) { return (int16_t)(M_d * sin8[(uint8_t)((uint16_t)M_a >> 8)]) >> 8; }
