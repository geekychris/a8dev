; a3d_math.s - multiply tables, matrices, vertex transform, projection.
;
; Quarter squares: a*b = T(a+b) - T(b-a), T(s) = floor(s*s/4), one table
; pair (low/high bytes) for s = -256..511 (768 entries each), C = &T(0).
;   unsigned a, b (0..255): pointers C+a and C-a, Y = b
;   signed a, b (-128..127): pointers C-128+a and C-128-a, Y = b^$80
; Both give the exact 16-bit product (signed: two's complement) with
;   sec / lda (m),y / sbc (m+2),y   (low)   lda (m+4),y / sbc (m+6),y (high)
; ~26 cycles a product, ~45 to set a.

        .importzp a3d_m, a3d_q, a3d_t
        .export   a3d_math_init, _a3d_math_init, mulset_u, mulset_s
        .export   _a3d_xform_, _a3d_project_, _a3d_mul8s_, _a3d_mulf_
        .export   _a3d_mat_mul_, _a3d_mma, _a3d_mmo
        .export   _a3d_rot16_, _a3d_rv, _a3d_rout
        .export   _a3d_xfirst, _a3d_xn, _a3d_om, _a3d_ox, _a3d_oy, _a3d_oz, _a3d_oshift
        .export   _a3d_vx, _a3d_vy, _a3d_vz, _a3d_sx, _a3d_sy, _a3d_vclip
        .export   _a3d_pxlo, _a3d_pxhi, _a3d_vcx, _a3d_vcy
        .export   _a3d_ma, _a3d_mb
        .export   a3d_rec_lo, a3d_rec_hi
        .export   mul_t_p, tx, pl, ph, r0, r1, r2

MAXV    = 64
NEAR    = 8
CLAMP   = $3FFF                 ; projected coordinates are kept within +-16383

.segment "RODATA"
sqlo:   .repeat 768, I
        .byte   <(((I-256)*(I-256))/4)
        .endrepeat
sqhi:   .repeat 768, I
        .byte   >(((I-256)*(I-256))/4)
        .endrepeat
SQC     = sqlo + 256            ; T(0)
SQB     = SQC - 128             ; signed base

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

.segment "ZEROPAGE"
s0:     .res 2                  ; the transform's three row sums
s1:     .res 2
s2:     .res 2

.segment "BSS"
_a3d_vx:    .res MAXV*2
_a3d_vy:    .res MAXV*2
_a3d_vz:    .res MAXV*2
_a3d_sx:    .res MAXV*2
_a3d_sy:    .res MAXV*2
_a3d_vclip: .res MAXV
_a3d_pxlo:  .res 256          ; f * 256 / z (z 0..255), built by a3d_view
_a3d_pxhi:  .res 256
_a3d_vcx:   .res 2            ; projection centre (int16)
_a3d_vcy:   .res 2
_a3d_om:    .res 9            ; object matrix (Q7)
_a3d_ox:    .res 2
_a3d_oy:    .res 2
_a3d_oz:    .res 2
_a3d_oshift: .res 1
_a3d_xfirst: .res 1
_a3d_xn:    .res 1
_a3d_ma:    .res 1
_a3d_mb:    .res 2
_a3d_rv:    .res 6            ; a3d_rot16 input vector
_a3d_rout:  .res 6            ; ... output
_a3d_mma:   .res 2            ; a3d_mat_mul: a, out
_a3d_mmo:   .res 2
mt:     .res 1
cnt:    .res 1
vidx:   .res 1                ; 2 * vertex index
kshift: .res 1                ; 7 - a3d_oshift
ext:    .res 1
tx:     .res 2
ty:     .res 2
tz:     .res 2
sgn:    .res 1
sgn2:   .res 1
nsh:    .res 1
r0:     .res 1
r1:     .res 1
r2:     .res 1
pl:     .res 1
ph:     .res 1
mmb:    .res 9                ; matrix b / object matrix, biased (^$80)
mout:   .res 9
mi:     .res 1
mk:     .res 1
mrow:   .res 1

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
.macro  MUL
        sec
        lda     (a3d_m),y
        sbc     (a3d_m+2),y
        tax
        lda     (a3d_m+4),y
        sbc     (a3d_m+6),y
.endmacro

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

; ------------------------------------------------------------------ matrices
; void a3d_mat_mul_(const a3d_mat *b): *a3d_mmo = *a3d_mma * b
; out[r][c] = (sum_k a[r][k] * b[k][c] + 64) >> 7, clamped to +-127.
; a and b are read before out is written: any of them may be the same.
_a3d_mat_mul_:
        sta     a3d_t
        stx     a3d_t+1
        ldy     #8
:       lda     (a3d_t),y
        eor     #$80
        sta     mmb,y
        dey
        bpl     :-
        lda     _a3d_mma
        sta     a3d_q
        lda     _a3d_mma+1
        sta     a3d_q+1
        lda     #0
        sta     mi                  ; row offset (0, 3, 6)
@row:   lda     #0
        sta     s0
        sta     s0+1
        sta     s1
        sta     s1+1
        sta     s2
        sta     s2+1
        sta     mk
@k:     lda     mi
        clc
        adc     mk
        tay
        lda     (a3d_q),y           ; a[r][k]
        jsr     mulset_s
        lda     mk                  ; b row k = mmb + 3k
        asl     a
        adc     mk
        sta     mrow
        tay
        lda     mmb,y
        tay
        MUL
        jsr     acc0
        ldy     mrow
        lda     mmb+1,y
        tay
        MUL
        jsr     acc1
        ldy     mrow
        lda     mmb+2,y
        tay
        MUL
        jsr     acc2
        inc     mk
        lda     mk
        cmp     #3
        bne     @k
        ldx     mi
        lda     s0
        ldy     s0+1
        jsr     q7
        sta     mout,x
        lda     s1
        ldy     s1+1
        jsr     q7
        sta     mout+1,x
        lda     s2
        ldy     s2+1
        jsr     q7
        sta     mout+2,x
        lda     mi
        clc
        adc     #3
        sta     mi
        cmp     #9
        beq     :+
        jmp     @row
:       lda     _a3d_mmo
        sta     a3d_q
        lda     _a3d_mmo+1
        sta     a3d_q+1
        ldy     #8
:       lda     mout,y
        sta     (a3d_q),y
        dey
        bpl     :-
        rts

; s += X (low) / A (high)
acc0:   clc
        adc     s0+1
        sta     s0+1
        txa
        clc
        adc     s0
        sta     s0
        bcc     :+
        inc     s0+1
:       rts
acc1:   clc
        adc     s1+1
        sta     s1+1
        txa
        clc
        adc     s1
        sta     s1
        bcc     :+
        inc     s1+1
:       rts
acc2:   clc
        adc     s2+1
        sta     s2+1
        txa
        clc
        adc     s2
        sta     s2
        bcc     :+
        inc     s2+1
:       rts

; A/Y = sum (low/high) -> A = (sum + 64) >> 7 clamped to +-127
q7:     clc
        adc     #64
        bcc     :+
        iny
:       asl     a                   ; carry = bit 7 of the low byte
        tya
        rol     a                   ; A = low byte of sum >> 7
        cpy     #$40
        bcc     @pos                ; high 0..$3F: positive, fits
        cpy     #$C0
        bcs     @neg                ; high $C0..$FF: negative, fits
        tya
        bmi     @mn
        lda     #127
        rts
@mn:    lda     #<-127
        rts
@pos:   rts
@neg:   cmp     #$80
        bne     :+
        lda     #<-127
:       rts

; void a3d_rot16_(const a3d_mat *m): a3d_rout = (m * a3d_rv) >> 7 (16-bit vector)
; each product |v| * |m| by hand, signed afterwards
_a3d_rot16_:
        sta     a3d_q
        stx     a3d_q+1
        ldx     #0                  ; output offset (0, 2, 4)
        ldy     #0                  ; matrix index
@row:   lda     #0
        sta     s0
        sta     s0+1
        sta     s1                  ; s1 = third byte of the sum
        stx     cnt
        sta     mk                  ; component 0..2
@c:     sty     mi
        lda     (a3d_q),y
        sta     sgn
        bpl     :+
        eor     #$FF
        clc
        adc     #1
:       sta     pl                  ; |m|
        lda     #0
        sta     ph
        lda     mk
        asl     a
        tax
        lda     _a3d_rv,x
        sta     tx
        lda     _a3d_rv+1,x
        sta     tx+1
        eor     sgn
        sta     sgn
        bit     tx+1
        bpl     :+
        jsr     neg_tx
:       jsr     mul_t_p             ; r = |v| * |m| >> 8
        bit     sgn
        bpl     @add
        sec
        lda     s0
        sbc     r0
        sta     s0
        lda     s0+1
        sbc     r1
        sta     s0+1
        lda     s1
        sbc     r2
        sta     s1
        jmp     @nx
@add:   clc
        lda     s0
        adc     r0
        sta     s0
        lda     s0+1
        adc     r1
        sta     s0+1
        lda     s1
        adc     r2
        sta     s1
@nx:    ldy     mi
        iny
        inc     mk
        lda     mk
        cmp     #3
        bne     @c
        ldx     cnt                 ; (v*m) >> 8, we want >> 7: times 2
        lda     s0
        asl     a
        sta     _a3d_rout,x
        lda     s0+1
        rol     a
        sta     _a3d_rout+1,x
        inx
        inx
        cpx     #6
        beq     :+
        jmp     @row
:       rts

; ------------------------------------------------------------------ projection
; vertex at vidx (2*index): a3d_vclip, a3d_sx, a3d_sy
proj_one:
        ldx     vidx
        txa
        lsr     a
        tay
        lda     _a3d_vz+1,x
        bmi     @behind
        bne     @front
        lda     _a3d_vz,x
        cmp     #NEAR
        bcs     @front
@behind:
        lda     #1
        sta     _a3d_vclip,y
        rts
@front: lda     #0
        sta     _a3d_vclip,y
        lda     _a3d_vx,x
        sta     tx
        lda     _a3d_vx+1,x
        sta     tx+1
        lda     _a3d_vy,x
        sta     ty
        lda     _a3d_vy+1,x
        sta     ty+1
        lda     _a3d_vz,x
        sta     tz
        ldy     #0                  ; z >> n until it fits a byte; the products >> n too
        lda     _a3d_vz+1,x
        beq     @normed
@norm:  lsr     a
        ror     tz
        iny
        cmp     #0
        bne     @norm
@normed:
        sty     nsh
        ldy     tz
        lda     _a3d_pxlo,y
        sta     pl
        lda     _a3d_pxhi,y
        sta     ph
        ; signs: x as is, y negated (screen y = cy - y * f / z)
        lda     tx+1
        sta     sgn
        bpl     :+
        jsr     neg_tx
:       lda     ty+1
        eor     #$80
        sta     sgn2
        bit     ty+1
        bpl     :+
        sec
        lda     #0
        sbc     ty
        sta     ty
        lda     #0
        sbc     ty+1
        sta     ty+1
:       lda     ph
        beq     :+
        jmp     @slow
:       ; ---- P < 256: one multiplier for both axes
        lda     pl
        jsr     mulset_u
        ldy     tx                  ; x: (|x| * P) >> 8 >> n
        jsr     mul16
        jsr     clamp_r
        ldx     vidx
        clc
        lda     _a3d_vcx
        adc     r0
        sta     _a3d_sx,x
        lda     _a3d_vcx+1
        adc     r1
        sta     _a3d_sx+1,x
        lda     ty
        sta     tx
        lda     ty+1
        sta     tx+1
        ldy     tx
        jsr     mul16
        lda     sgn2
        sta     sgn
        jsr     clamp_r
        ldx     vidx
        clc
        lda     _a3d_vcy
        adc     r0
        sta     _a3d_sy,x
        lda     _a3d_vcy+1
        adc     r1
        sta     _a3d_sy+1,x
        rts
@slow:  jsr     mul_t_p
        jsr     shr_r
        jsr     clamp_r
        ldx     vidx
        clc
        lda     _a3d_vcx
        adc     r0
        sta     _a3d_sx,x
        lda     _a3d_vcx+1
        adc     r1
        sta     _a3d_sx+1,x
        lda     ty
        sta     tx
        lda     ty+1
        sta     tx+1
        lda     sgn2
        sta     sgn
        jsr     mul_t_p
        jsr     shr_r
        jsr     clamp_r
        ldx     vidx
        clc
        lda     _a3d_vcy
        adc     r0
        sta     _a3d_sy,x
        lda     _a3d_vcy+1
        adc     r1
        sta     _a3d_sy+1,x
        rts

; r2:r1:r0 = (tx (16) * the set multiplier) >> 8 >> nsh; Y = tx low on entry
mul16:  MUL
        sta     r0
        lda     #0
        sta     r1
        sta     r2
        ldy     tx+1
        beq     shr_r
        MUL
        sta     r1
        txa
        clc
        adc     r0
        sta     r0
        bcc     shr_r
        inc     r1
        bne     shr_r
        inc     r2
; r >>= nsh
shr_r:  ldx     nsh
        beq     @done
@s:     lsr     r2
        ror     r1
        ror     r0
        dex
        bne     @s
@done:  rts

; void a3d_project_(uint8 n): vertices a3d_xfirst.. from a3d_v*
_a3d_project_:
        sta     cnt
        tax
        beq     @done
        lda     _a3d_xfirst
        asl     a
        sta     vidx
@loop:  jsr     proj_one
        inc     vidx
        inc     vidx
        dec     cnt
        bne     @loop
@done:  rts

; ------------------------------------------------------------------ transform
; row sum `sum` (+)= m[ent] * va; Y = m entry ^ $80 (patched)
.macro  PROD    ent, sum, first
ent = * + 1
        ldy     #0
        MUL
  .if first
        sta     sum+1
        stx     sum
  .else
        tay
        txa
        clc
        adc     sum
        sta     sum
        tya
        adc     sum+1
        sta     sum+1
  .endif
.endmacro

; the sum at zero page s0+X (X = 0, 2, 4) >>= kshift (arithmetic)
shift_sum:
        ldy     kshift
        cpy     #7
        bne     @gen
        lda     s0,x                ; >> 7 rounded: (sum + 64) << 1 >> 8
        clc
        adc     #64
        sta     s0,x
        bcc     :+
        inc     s0+1,x
:       asl     a
        lda     s0+1,x
        rol     a
        sta     s0,x
        lda     #0
        adc     #$FF
        eor     #$FF
        sta     s0+1,x
        rts
@gen:   cpy     #0
        beq     @out
        lda     s0+1,x
@r:     cmp     #$80
        ror     a
        ror     s0,x
        dey
        bne     @r
        sta     s0+1,x
@out:   rts

; void a3d_xform_(const int8 *verts): a3d_xn vertices into a3d_xfirst..
_a3d_xform_:
        sta     a3d_q
        stx     a3d_q+1
        ldx     #8
:       lda     _a3d_om,x
        eor     #$80
        sta     mmb,x
        dex
        bpl     :-
        lda     mmb+0
        sta     e0
        lda     mmb+1
        sta     e1
        lda     mmb+2
        sta     e2
        lda     mmb+3
        sta     e3
        lda     mmb+4
        sta     e4
        lda     mmb+5
        sta     e5
        lda     mmb+6
        sta     e6
        lda     mmb+7
        sta     e7
        lda     mmb+8
        sta     e8
        lda     #7
        sec
        sbc     _a3d_oshift
        sta     kshift
        lda     _a3d_xfirst
        asl     a
        sta     vidx
        lda     _a3d_xn
        sta     cnt
        bne     xf_vloop
        rts
xf_vloop:
        ldy     #0
        lda     (a3d_q),y
        jsr     mulset_s
        PROD    e0, s0, 1
        PROD    e3, s1, 1
        PROD    e6, s2, 1
        ldy     #1
        lda     (a3d_q),y
        jsr     mulset_s
        PROD    e1, s0, 0
        PROD    e4, s1, 0
        PROD    e7, s2, 0
        ldy     #2
        lda     (a3d_q),y
        jsr     mulset_s
        PROD    e2, s0, 0
        PROD    e5, s1, 0
        PROD    e8, s2, 0
        ldx     #0
        jsr     shift_sum
        ldx     #2
        jsr     shift_sum
        ldx     #4
        jsr     shift_sum
        ldx     vidx
        clc
        lda     s0
        adc     _a3d_ox
        sta     _a3d_vx,x
        lda     s0+1
        adc     _a3d_ox+1
        sta     _a3d_vx+1,x
        clc
        lda     s1
        adc     _a3d_oy
        sta     _a3d_vy,x
        lda     s1+1
        adc     _a3d_oy+1
        sta     _a3d_vy+1,x
        clc
        lda     s2
        adc     _a3d_oz
        sta     _a3d_vz,x
        lda     s2+1
        adc     _a3d_oz+1
        sta     _a3d_vz+1,x
        jsr     proj_one
        clc
        lda     a3d_q
        adc     #3
        sta     a3d_q
        bcc     :+
        inc     a3d_q+1
:       inc     vidx
        inc     vidx
        dec     cnt
        beq     :+
        jmp     xf_vloop
:       rts

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
