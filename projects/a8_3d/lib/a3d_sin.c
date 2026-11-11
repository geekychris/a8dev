/* a3d_sin.c - the Q7 sine table and the shade patterns. */
#include "a3d_int.h"

const int8_t a3d_sin8[320] = {
#include "a3d_sin.inc"
};

const uint8_t a3d_shade[7][2] = {
    { 0x00, 0x00 }, { 0x11, 0x44 }, { 0x55, 0x55 }, { 0x66, 0x99 },
    { 0xAA, 0xAA }, { 0xBB, 0xEE }, { 0xFF, 0xFF }
};
