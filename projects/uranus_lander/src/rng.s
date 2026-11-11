; Uranus Lander: the original's random numbers, fast.
;
;   rng_state = rng_state * 1103515245 + 12345;  return (rng_state >> 16) & 0x7FFF
;
; The same LCG as the original (so every level's terrain is the Amiga's),
; but the 32 x 32 multiply (mod 2^32) is ten 8 x 8 partial products from
; tables of each byte of the constant times 0..255 (tables.c, 1,792 bytes):
; ~200 cycles a call instead of ~5,000 in cc65's C.

        .export _rng_next, _rng_state
        .import _mc0_lo, _mc0_hi, _mc1_lo, _mc1_hi, _mc2_lo, _mc2_hi, _mc3_lo

        .bss
_rng_state: .res 4
r:          .res 4

        .code

; returns A/X = (new state >> 16) & $7FFF
_rng_next:
        ldx _rng_state          ; s0 terms: s0*c0 (0,1) s0*c1 (1,2) s0*c2 (2,3) s0*c3 (3)
        lda _mc0_lo,x
        sta r
        lda _mc0_hi,x
        sta r+1
        lda _mc1_hi,x
        sta r+2
        lda _mc2_hi,x
        sta r+3
        clc
        lda r+1
        adc _mc1_lo,x
        sta r+1
        lda r+2
        adc _mc2_lo,x
        sta r+2
        lda r+3
        adc _mc3_lo,x
        sta r+3
        ldx _rng_state+1        ; s1 terms: s1*c0 (1,2) s1*c1 (2,3) s1*c2 (3)
        clc
        lda r+1
        adc _mc0_lo,x
        sta r+1
        lda r+2
        adc _mc0_hi,x
        sta r+2
        lda r+3
        adc _mc1_hi,x
        sta r+3
        clc
        lda r+2
        adc _mc1_lo,x
        sta r+2
        lda r+3
        adc _mc2_lo,x
        sta r+3
        ldx _rng_state+2        ; s2 terms: s2*c0 (2,3) s2*c1 (3)
        clc
        lda r+2
        adc _mc0_lo,x
        sta r+2
        lda r+3
        adc _mc0_hi,x
        clc
        adc _mc1_lo,x
        sta r+3
        ldx _rng_state+3        ; s3*c0 (3)
        lda r+3
        clc
        adc _mc0_lo,x
        sta r+3
        clc                     ; + 12345
        lda r
        adc #<12345
        sta _rng_state
        lda r+1
        adc #>12345
        sta _rng_state+1
        lda r+2
        adc #0
        sta _rng_state+2
        lda r+3
        adc #0
        sta _rng_state+3
        and #$7F
        tax
        lda _rng_state+2
        rts
