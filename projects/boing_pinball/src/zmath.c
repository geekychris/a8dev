/* Boing Pinball's own maths: the Q14 sine of an angle in 1/4096 radian
   and the original's random numbers (the multiplies are the SDK's
   fixmath.h). */
#include <stdint.h>
#include "pbz.h"

/* sin_q14 (A, 1/4096 rad -> Q14, from the quarter table in sin_tab.c) is
   physasm.s's, step for step what the C here did. */
