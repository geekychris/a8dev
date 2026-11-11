; a3d_math.s - multiply tables, matrices, vertex transform, projection.
;
; Quarter squares: a*b = T(a+b) - T(b-a), T(s) = floor(s*s/4), one table
; pair (low/high bytes) for s = -256..511 (768 entries each), C = &T(0).
;   unsigned a, b (0..255): pointers C+a and C-a, Y = b
;   signed a, b (-128..127): pointers C-128+a and C-128-a, Y = b^$80
; Both give the exact 16-bit product (signed: two's complement) with
;   sec / lda (m),y / sbc (m+2),y   (low)   lda (m+4),y / sbc (m+6),y (high)
; ~26 cycles a product, ~45 to set a.

; a3d_mul.s: the tables, setting a, the C helpers (a3d_mul8s, a3d_mulf,
; a3d_muldiv) and mul_t_p (16 x 16 >> 8), used by polygons and the transform.

        .importzp a3d_m, a3d_t
        .constructor a3d_mul_init, 20
        .export   a3d_math_init, _a3d_math_init, mulset_u, mulset_s
        .export   _a3d_mul8s_, _a3d_mulf_, _a3d_ma, _a3d_mb
        .export   a3d_rec_lo, a3d_rec_hi
        .export   mul_t_p, neg_tx, clamp_r, mt, ext, tx, ty, sgn, pl, ph, r0, r1, r2

        .include "a3d_mul.inc"

CLAMP   = $3FFF                 ; projected coordinates are kept within +-16383

; the tables are made at start-up (a constructor): 1.5K less to load. A
; game with the low-RAM segment (its own linker config) puts them there
; with -DA3D_LOWTAB.
.ifdef A3D_LOWTAB
.segment "LOWRAM"
.else
.ifdef A3D_LOWTAB
.segment "LOWRAM"
.else
.segment "BSS"
.endif
.endif
sqlo:   .res    768
sqhi:   .res    768
SQC     = sqlo + 256            ; T(0)
SQB     = SQC - 128             ; signed base

.segment "RODATA"

; 65536 / dy for edge slopes (dy 1..96; [1] = $FFFF)
a3d_rec_lo:
        .byte   0, $FF
        .repeat 95, I
        .byte   <(65536 / (I+2))
        .endrepeat
a3d_rec_hi:
        .byte   0, $FF
        .repeat 95, I
        .byte   >(65536 / (I+2))
        .endrepeat

.segment "BSS"
_a3d_ma:    .res 1
_a3d_mb:    .res 2
mt:     .res 1
ext:    .res 1
tx:     .res 2
ty:     .res 2
sgn:    .res 1
r0:     .res 1
r1:     .res 1
r2:     .res 1
pl:     .res 1
ph:     .res 1

.segment "ONCE"

; T(s) = s*s/4 for s = 0..511 at sqlo+256 (s*s by adding the odd numbers),
; then T(-s) = T(s) for s = 1..256 below it
a3d_mul_init:
        lda     #0
        sta     a3d_t               ; s*s, 24 bits
        sta     a3d_t+1
        sta     a3d_t+2
        sta     a3d_t+4             ; the next odd number, 16 bits
        lda     #1
        sta     a3d_t+3
        lda     #<(sqlo+256)
        sta     a3d_t+5
        lda     #>(sqlo+256)
        sta     a3d_t+6
        lda     #<(sqhi+256)
        sta     a3d_t+7
        lda     #>(sqhi+256)
        sta     a3d_t+8
        lda     #0                  ; 512 values
        sta     a3d_t+11
        lda     #2
        sta     a3d_t+12
@l:     lda     a3d_t+1             ; (s*s) >> 2
        sta     a3d_t+9
        lda     a3d_t+2
        sta     a3d_t+10
        lda     a3d_t
        lsr     a3d_t+10
        ror     a3d_t+9
        ror     a
        lsr     a3d_t+10
        ror     a3d_t+9
        ror     a
        ldy     #0
        sta     (a3d_t+5),y
        lda     a3d_t+9
        sta     (a3d_t+7),y
        clc                         ; s*s += odd; odd += 2
        lda     a3d_t
        adc     a3d_t+3
        sta     a3d_t
        lda     a3d_t+1
        adc     a3d_t+4
        sta     a3d_t+1
        bcc     :+
        inc     a3d_t+2
:       clc
        lda     a3d_t+3
        adc     #2
        sta     a3d_t+3
        bcc     :+
        inc     a3d_t+4
:       inc     a3d_t+5
        bne     :+
        inc     a3d_t+6
:       inc     a3d_t+7
        bne     :+
        inc     a3d_t+8
:       lda     a3d_t+11
        bne     :+
        dec     a3d_t+12
:       dec     a3d_t+11
        lda     a3d_t+11
        ora     a3d_t+12
        bne     @l
        ldx     #0                  ; sq[i] = sq[512 - i], i = 0..255
        ldy     #255
@m:     lda     sqlo+257,y
        sta     sqlo,x
        lda     sqhi+257,y
        sta     sqhi,x
        inx
        dey
        cpx     #0
        bne     @m
        rts

.segment "CODE"

a3d_math_init:
_a3d_math_init:
        rts

; A = a, unsigned
mulset_u:
        sta     mt
        clc
        adc     #<SQC
        sta     a3d_m
        sta     a3d_m+4
        lda     #>SQC
        adc     #0
        sta     a3d_m+1
        adc     #3                  ; +768: the high table (carry clear)
        sta     a3d_m+5
        sec
        lda     #<SQC
        sbc     mt
        sta     a3d_m+2
        sta     a3d_m+6
        lda     #>SQC
        sbc     #0
        sta     a3d_m+3
        clc
        adc     #3
        sta     a3d_m+7
        rts

; A = a, signed (then Y = b ^ $80)
mulset_s:
        sta     mt
        ldx     #0
        cmp     #$80
        bcc     :+
        dex                         ; X = sign extension
:       stx     ext
        clc
        adc     #<SQB
        sta     a3d_m
        sta     a3d_m+4
        txa
        adc     #>SQB
        sta     a3d_m+1
        clc
        adc     #3
        sta     a3d_m+5
        sec
        lda     #<SQB
        sbc     mt
        sta     a3d_m+2
        sta     a3d_m+6
        lda     #>SQB
        sbc     ext
        sta     a3d_m+3
        clc
        adc     #3
        sta     a3d_m+7
        rts

; product: X = low, A = high
; ------------------------------------------------------------------ C helpers
; int16 a3d_mul8s_(int8 b): a3d_ma * b, signed
_a3d_mul8s_:
        eor     #$80
        pha
        lda     _a3d_ma
        jsr     mulset_s
        pla
        tay
        MUL
        stx     mt
        tax
        lda     mt
        rts

; int16 a3d_mulf_(int16 b): (a3d_mb * b) >> 8, signed 16 x 16 (result clamped)
_a3d_mulf_:
        sta     tx
        stx     tx+1
        txa
        eor     _a3d_mb+1
        sta     sgn
        bit     tx+1
        bpl     :+
        jsr     neg_tx
:       lda     _a3d_mb
        sta     pl
        lda     _a3d_mb+1
        sta     ph
        bpl     :+
        sec
        lda     #0
        sbc     pl
        sta     pl
        lda     #0
        sbc     ph
        sta     ph
:       jsr     mul_t_p
        jsr     clamp_r
        lda     r0
        ldx     r1
        rts

neg_tx: sec
        lda     #0
        sbc     tx
        sta     tx
        lda     #0
        sbc     tx+1
        sta     tx+1
        rts

; r2:r1:r0 = (tx (16, unsigned) * ph:pl) >> 8
mul_t_p:
        lda     pl
        jsr     mulset_u
        ldy     tx
        MUL                         ; pl*tlo: high in A
        sta     r0
        lda     #0
        sta     r1
        sta     r2
        ldy     tx+1
        beq     @hi
        MUL                         ; pl*thi: X low, A high
        sta     r1
        txa
        clc
        adc     r0
        sta     r0
        bcc     @hi
        inc     r1
        bne     @hi
        inc     r2
@hi:    lda     ph
        beq     @done
        jsr     mulset_u
        ldy     tx
        MUL                         ; ph*tlo: X -> r0, A -> r1
        sta     mt
        txa
        clc
        adc     r0
        sta     r0
        lda     mt
        adc     r1
        sta     r1
        bcc     :+
        inc     r2
:       ldy     tx+1
        beq     @done
        MUL                         ; ph*thi: X -> r1, A -> r2
        sta     mt
        txa
        clc
        adc     r1
        sta     r1
        lda     mt
        adc     r2
        sta     r2
@done:  rts

; r (unsigned 24) -> at most CLAMP, then negated if sgn bit 7
clamp_r:
        lda     r2
        bne     @big
        lda     r1
        cmp     #>CLAMP+1
        bcc     @ok
@big:   lda     #<CLAMP
        sta     r0
        lda     #>CLAMP
        sta     r1
@ok:    bit     sgn
        bpl     @pos
        sec
        lda     #0
        sbc     r0
        sta     r0
        lda     #0
        sbc     r1
        sta     r1
@pos:   rts

; ------------------------------------------------------------------ muldiv
; int16 a3d_muldiv_(int16 c): (a3d_mda * a3d_mdb) / c, signed, truncated
; toward zero, clamped to +-32767 (c != 0). ~700 cycles.
        .export _a3d_muldiv_, _a3d_mda, _a3d_mdb
.segment "BSS"
_a3d_mda: .res 2
_a3d_mdb: .res 2
md_a:   .res 2
md_b:   .res 2
md_d:   .res 2
md_p:   .res 4
md_s:   .res 1
.segment "CODE"
md_abs: ; A/X -> |A/X| in A/X, sign into md_s
        cpx     #$80
        bcc     :+
        pha
        lda     md_s
        eor     #$80
        sta     md_s
        pla
        eor     #$FF
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
:       rts

_a3d_muldiv_:
        ldy     #0
        sty     md_s
        jsr     md_abs
        sta     md_d
        stx     md_d+1
        lda     _a3d_mda
        ldx     _a3d_mda+1
        jsr     md_abs
        sta     md_a
        stx     md_a+1
        lda     _a3d_mdb
        ldx     _a3d_mdb+1
        jsr     md_abs
        sta     md_b
        stx     md_b+1
        ; p = a * b (32 bits)
        lda     md_b
        jsr     mulset_u
        ldy     md_a
        MUL
        stx     md_p
        sta     md_p+1
        ldy     md_a+1
        MUL
        sta     md_p+2
        txa
        clc
        adc     md_p+1
        sta     md_p+1
        lda     #0
        sta     md_p+3
        adc     md_p+2
        sta     md_p+2
        lda     md_b+1
        beq     @div
        jsr     mulset_u
        ldy     md_a
        MUL                         ; -> p1, p2
        sta     mt
        txa
        clc
        adc     md_p+1
        sta     md_p+1
        lda     mt
        adc     md_p+2
        sta     md_p+2
        bcc     :+
        inc     md_p+3
:       ldy     md_a+1
        MUL                         ; -> p2, p3
        sta     mt
        txa
        clc
        adc     md_p+2
        sta     md_p+2
        lda     mt
        adc     md_p+3
        sta     md_p+3
@div:   ; overflow if p >> 16 >= d (the quotient would not fit 16 bits)
        lda     md_p+2
        cmp     md_d
        lda     md_p+3
        sbc     md_d+1
        bcs     @big
        ldx     #16
@l:     asl     md_p
        rol     md_p+1
        rol     md_p+2
        rol     md_p+3
        bcs     @sub                ; 17-bit remainder: always >= d
        lda     md_p+2
        cmp     md_d
        lda     md_p+3
        sbc     md_d+1
        bcc     @nx
@sub:   lda     md_p+2
        sec
        sbc     md_d
        sta     md_p+2
        lda     md_p+3
        sbc     md_d+1
        sta     md_p+3
        inc     md_p                ; quotient bit
@nx:    dex
        bne     @l
        lda     md_p+1
        bmi     @big                ; >= 32768
        tax
        lda     md_p
        jmp     @sign
@big:   lda     #$FF
        ldx     #$7F
@sign:  bit     md_s
        bpl     :+
        eor     #$FF
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
:       rts

