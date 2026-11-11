; fast.s - a pixel in the back buffer: plot_(colour 0..3) at pl_x (0..159),
; pl_y (0..87). (The ring's dots and the radar: cheaper than a3d's lines,
; and a3d_line isn't linked.)
        .importzp a3d_p, a3d_bsel
        .import   _a3d_ylo, _a3d_yhi
        .export   _pl_x, _pl_y, _plot_, _radar_clear

.segment "LOWRAM"
_pl_x:  .res 1
_pl_y:  .res 1
pc:     .res 1

.segment "FRONTRO"
pmask:  .byte   $C0, $30, $0C, $03
cfill:  .byte   $00, $55, $AA, $FF

.segment "FRONT"
; void radar_clear(void): the radar's box (bytes 32-39, lines 0-24) to 0:
; plot() leaves no dirty rectangle, so its dots go this way
_radar_clear:
        ldx     #24
@l:     lda     _a3d_ylo,x
        sta     a3d_p
        lda     _a3d_yhi,x
        ora     a3d_bsel
        sta     a3d_p+1
        lda     #0
        ldy     #39
:       sta     (a3d_p),y
        dey
        cpy     #31
        bne     :-
        dex
        bpl     @l
        rts

_plot_: tax
        lda     cfill,x
        sta     pc
        ldy     _pl_y
        lda     _a3d_ylo,y
        sta     a3d_p
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_p+1
        lda     _pl_x
        and     #3
        tax
        lda     _pl_x
        lsr     a
        lsr     a
        tay
        lda     (a3d_p),y               ; new = old ^ ((old ^ colour) & mask)
        eor     pc
        and     pmask,x
        eor     (a3d_p),y
        sta     (a3d_p),y
        rts

; void cam_xz(void): a world point (tc_x, tc_z, units) into camera space:
; rel[0] = dx cos - dz sin, rel[2] = dx sin + dz cos (dx, dz from the
; camera tc_cx, tc_cz; cos, sin Q7 in tc_c, tc_s): four 16 x 8 products
; from a3d's quarter-square tables
        .importzp a3d_m
        .import   mulset_u
        .export   _tc_x, _tc_z, _tc_cx, _tc_cz, _tc_c, _tc_s, _tc_rel, _cam_xz
        .include  "a3d_mul.inc"

.segment "LOWRAM"
_tc_x:  .res 2
_tc_z:  .res 2
_tc_cx: .res 2
_tc_cz: .res 2
_tc_c:  .res 1
_tc_s:  .res 1
_tc_rel: .res 6
dxl:    .res 1
dxh:    .res 1
dzl:    .res 1
dzh:    .res 1
ml:     .res 1                          ; |v|
mh:     .res 1
sg:     .res 1
r0:     .res 1
r1:     .res 1
r2:     .res 1
pl:     .res 1                          ; a product
ph:     .res 1

.segment "FRONT"
; (A = v low, X = v high) * (Y = q, Q7 signed) / 128 -> pl, ph (signed)
mq7:    stx     sg
        cpx     #$80
        bcc     :+
        eor     #$FF                    ; |v|
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
:       sta     ml
        stx     mh
        tya
        bpl     :+
        eor     #$FF
        clc
        adc     #1
        tay
        lda     sg
        eor     #$80
        sta     sg
        tya
:       jsr     mulset_u                ; |q|
        ldy     ml
        MUL
        stx     r0
        sta     r1
        ldy     mh
        MUL
        sta     r2
        txa
        clc
        adc     r1
        sta     r1
        bcc     :+
        inc     r2
:       asl     r0                      ; (r2:r1:r0) >> 7
        rol     r1
        rol     r2
        lda     r1
        ldx     r2
        bit     sg
        bpl     :+
        eor     #$FF                    ; negative
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
:       sta     pl
        stx     ph
        rts

_cam_xz:
        sec
        lda     _tc_x
        sbc     _tc_cx
        sta     dxl
        lda     _tc_x+1
        sbc     _tc_cx+1
        sta     dxh
        sec
        lda     _tc_z
        sbc     _tc_cz
        sta     dzl
        lda     _tc_z+1
        sbc     _tc_cz+1
        sta     dzh
        ; X = dx cos - dz sin
        lda     dxl
        ldx     dxh
        ldy     _tc_c
        jsr     mq7
        lda     pl
        sta     _tc_rel
        lda     ph
        sta     _tc_rel+1
        lda     dzl
        ldx     dzh
        ldy     _tc_s
        jsr     mq7
        sec
        lda     _tc_rel
        sbc     pl
        sta     _tc_rel
        lda     _tc_rel+1
        sbc     ph
        sta     _tc_rel+1
        ; Z = dx sin + dz cos
        lda     dxl
        ldx     dxh
        ldy     _tc_s
        jsr     mq7
        lda     pl
        sta     _tc_rel+4
        lda     ph
        sta     _tc_rel+5
        lda     dzl
        ldx     dzh
        ldy     _tc_c
        jsr     mq7
        clc
        lda     _tc_rel+4
        adc     pl
        sta     _tc_rel+4
        lda     _tc_rel+5
        adc     ph
        sta     _tc_rel+5
        rts
