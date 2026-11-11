/* made by tools/gen_course.py - do not edit */
#include "rs.h"

/* each course: its packed picture, grid and numbers in the banks
   (bank, address), the map's width and rows, its fonts, its name */
const CourseSrc course_src[NCOURSE] = {
    { 0, 0x4000, 0, 0x537F, 0, 0x5F46, 128, 42, 6, "WARM-UP" },
    { 0, 0x62A2, 0, 0x7A6E, 1, 0x49F1, 169, 51, 7, "FIRST DROP" },
    { 1, 0x4F44, 1, 0x6AA2, 1, 0x7A01, 190, 49, 8, "SWITCHBACK" },
    { 2, 0x400E, 2, 0x541B, 2, 0x6264, 177, 63, 6, "SKYWAY" },
    { 2, 0x6742, 2, 0x7BEC, 3, 0x4A80, 59, 90, 5, "TOPSY-TURVY" },
    { 3, 0x4FB5, 3, 0x67BA, 3, 0x7712, 186, 56, 7, "THE GAUNTLET" },
};
