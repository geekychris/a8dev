; a3d_prism.s - upright convex objects in one call, all in asm: a box turned
; about the vertical (a3d_prism), a square pyramid (a3d_pyramid) and an
; upright quad between two floor points (a3d_wallquad). The footprint's
; corners are turned and placed in camera space, projected once (a3d's
; projection), and the faces that look at the camera go straight to the
; polygon fill, each in its own shade. No C between the faces.
;
; In: the a3d_pr_* globals (a3d.h). Uses a3d's vertices 0-7 (a3d_vx ... a3d_sy).
; Returns A = 0 when drawn, 1 when a corner is nearer than the near plane
; (nothing drawn: the caller clips it some other way).
;
; The footprint, in the object's own units (x across, z along it):
;   corner 0 (-hw, z0), 1 (+hw, z0), 2 (+hw, z1), 3 (-hw, z1)
; turned by (c, s) Q7 into camera space: X = x c - z s, Z = x s + z c, plus
; the centre (a3d_pr_x, a3d_pr_z). The sides: 0 = z0 (the back), 1 = +x
; (right), 2 = z1 (the front), 3 = -x (left), 4 = the top; a3d_pr_vis bit n
; for side n (0 = work it out: a side is seen when its corners go left to
; right on the screen, the top when it is below the eye). a3d_pr_shade:
; back, front, left, right, top as a3d_shade indices (0-6).

        .importzp a3d_m
        .import   _a3d_vx, _a3d_vy, _a3d_vz, _a3d_sx, _a3d_sy, _a3d_xfirst, _a3d_project_
        .import   _a3d_px, _a3d_py, _a3d_poly, _a3d_shade, _a3d_pat_e, _a3d_pat_o
        .import   mulset_s
        .include "a3d_mul.inc"
        .export   _a3d_pr_x, _a3d_pr_z, _a3d_pr_c, _a3d_pr_s, _a3d_pr_hw, _a3d_pr_z0, _a3d_pr_z1
        .export   _a3d_pr_y0, _a3d_pr_y1, _a3d_pr_shade, _a3d_pr_vis, _a3d_pr_far
        .export   _a3d_pr_ax, _a3d_pr_az, _a3d_pr_bx, _a3d_pr_bz
        .export   _a3d_prism, _a3d_pyramid, _a3d_wallquad

NEAR    = 8                     ; (as a3d_xform.s)

.ifdef A3D_LOWTAB
.segment "LOWRAM"
.else
.segment "BSS"
.endif
_a3d_pr_x:      .res 2
_a3d_pr_z:      .res 2
_a3d_pr_c:      .res 1
_a3d_pr_s:      .res 1
_a3d_pr_hw:     .res 1
_a3d_pr_z0:     .res 1
_a3d_pr_z1:     .res 1
_a3d_pr_y0:     .res 2
_a3d_pr_y1:     .res 2
_a3d_pr_shade:  .res 5
_a3d_pr_vis:    .res 1
_a3d_pr_far:    .res 1
_a3d_pr_ax:     .res 2
_a3d_pr_az:     .res 2
_a3d_pr_bx:     .res 2
_a3d_pr_bz:     .res 2
chw:    .res 2                  ; c hw, s hw, c z0, c z1, s z0, s z1 (Q7)
shw:    .res 2
cz:     .res 4
szz:    .res 4
t0:     .res 2
hn:     .res 1                  ; $FF: -hw
zi:     .res 1
kk:     .res 1
vis:    .res 1
side:   .res 1
apex:   .res 1                  ; 1: the pyramid (the top corners are the apex)
ci:     .res 1                  ; corner pair for quad: a, b (x2)
cj:     .res 1

.segment "RODATA"
; side n: its corners (x2) and its shade's place in a3d_pr_shade
s_a:    .byte   0, 2, 4, 6
s_b:    .byte   2, 4, 6, 0
s_sh:   .byte   0, 3, 1, 2

.segment "CODE"
; ------------------------------------------------------------------ the footprint
; the six products
prods:  lda     _a3d_pr_c
        jsr     mulset_s
        lda     _a3d_pr_hw
        eor     #$80
        tay
        MUL
        stx     chw
        sta     chw+1
        lda     _a3d_pr_z0
        eor     #$80
        tay
        MUL
        stx     cz
        sta     cz+1
        lda     _a3d_pr_z1
        eor     #$80
        tay
        MUL
        stx     cz+2
        sta     cz+3
        lda     _a3d_pr_s
        jsr     mulset_s
        lda     _a3d_pr_hw
        eor     #$80
        tay
        MUL
        stx     shw
        sta     shw+1
        lda     _a3d_pr_z0
        eor     #$80
        tay
        MUL
        stx     szz
        sta     szz+1
        lda     _a3d_pr_z1
        eor     #$80
        tay
        MUL
        stx     szz+2
        sta     szz+3
        rts

; t0 = -t0
neg_t0: sec
        lda     #0
        sbc     t0
        sta     t0
        lda     #0
        sbc     t0+1
        sta     t0+1
        rts

; t0 = t0 >> 7 (arithmetic; |t0| < 32768 so the result fits 9 bits)
asr7:   lda     t0+1
        asl     t0
        rol     a
        sta     t0
        lda     #0
        bcc     :+
        lda     #$FF
:       sta     t0+1
        rts

; corner kk (x2) from hn (+-hw) and zi (z0 / z1, x2): a3d_vx, vz at kk and
; kk + 8 (the top), a3d_vy y0 / y1
corner: lda     chw                 ; X = (+-c hw) - s z
        sta     t0
        lda     chw+1
        sta     t0+1
        bit     hn
        bpl     :+
        jsr     neg_t0
:       ldy     zi
        sec
        lda     t0
        sbc     szz,y
        sta     t0
        lda     t0+1
        sbc     szz+1,y
        sta     t0+1
        jsr     asr7
        ldx     kk
        clc
        lda     t0
        adc     _a3d_pr_x
        sta     _a3d_vx,x
        sta     _a3d_vx+8,x
        lda     t0+1
        adc     _a3d_pr_x+1
        sta     _a3d_vx+1,x
        sta     _a3d_vx+9,x
        lda     shw                 ; Z = (+-s hw) + c z
        sta     t0
        lda     shw+1
        sta     t0+1
        bit     hn
        bpl     :+
        jsr     neg_t0
:       ldy     zi
        clc
        lda     t0
        adc     cz,y
        sta     t0
        lda     t0+1
        adc     cz+1,y
        sta     t0+1
        jsr     asr7
        ldx     kk
        clc
        lda     t0
        adc     _a3d_pr_z
        sta     _a3d_vz,x
        sta     _a3d_vz+8,x
        lda     t0+1
        adc     _a3d_pr_z+1
        sta     _a3d_vz+1,x
        sta     _a3d_vz+9,x
        lda     _a3d_pr_y0
        sta     _a3d_vy,x
        lda     _a3d_pr_y0+1
        sta     _a3d_vy+1,x
        lda     _a3d_pr_y1
        sta     _a3d_vy+8,x
        lda     _a3d_pr_y1+1
        sta     _a3d_vy+9,x
        rts

; the four corners; carry set if one is nearer than NEAR
footprint:
        jsr     prods
        ldx     #3
@c:     lda     cn_k,x
        sta     kk
        lda     cn_h,x
        sta     hn
        lda     cn_z,x
        sta     zi
        txa
        pha
        jsr     corner
        pla
        tax
        dex
        bpl     @c
        ldx     #6
@n:     lda     _a3d_vz+1,x         ; z < NEAR (or negative): too near
        bmi     @near
        bne     :+
        lda     _a3d_vz,x
        cmp     #NEAR
        bcc     @near
:       dex
        dex
        bpl     @n
        clc
        rts
@near:  sec
        rts

.segment "RODATA"
cn_k:   .byte   0, 2, 4, 6
cn_h:   .byte   $FF, 0, 0, $FF
cn_z:   .byte   0, 0, 2, 2
.segment "CODE"

; the pattern for shade A (0-6)
shade:  asl     a
        tax
        lda     _a3d_shade,x
        sta     _a3d_pat_e
        lda     _a3d_shade+1,x
        sta     _a3d_pat_o
        rts

; a3d_px/py[n] = a3d_sx/sy[X/2], Y = n * 2
putv:   lda     _a3d_sx,x
        sta     _a3d_px,y
        lda     _a3d_sx+1,x
        sta     _a3d_px+1,y
        lda     _a3d_sy,x
        sta     _a3d_py,y
        lda     _a3d_sy+1,x
        sta     _a3d_py+1,y
        rts

; N set if sx[ci] < sx[cj] (signed)
leftof: ldx     ci
        ldy     cj
        lda     _a3d_sx,x
        cmp     _a3d_sx,y
        lda     _a3d_sx+1,x
        sbc     _a3d_sx+1,y
        bvc     :+
        eor     #$80
:       rts

; the quad top ci, top cj, foot cj, foot ci (the pyramid: apex, cj, ci)
quad:   ldy     #0
        ldx     ci
        lda     apex
        beq     :+
        ldx     #8                  ; (the apex is vertex 4)
        jsr     putv
        ldy     #2
        bne     @foot
:       txa
        ora     #8
        tax
        jsr     putv
        ldy     #2
        lda     cj
        ora     #8
        tax
        jsr     putv
        ldy     #4
@foot:  ldx     cj
        jsr     putv
        iny
        iny
        ldx     ci
        jsr     putv
        iny
        iny
        tya
        lsr     a
        jmp     _a3d_poly

; ------------------------------------------------------------------ the calls
; uint8 a3d_prism(void)
_a3d_prism:
        lda     #0
        sta     apex
        jsr     footprint
        bcc     :+
        lda     #1
        ldx     #0
        rts
:       lda     #0
        sta     _a3d_xfirst
        lda     #8
        jsr     _a3d_project_
        lda     _a3d_pr_far
        beq     :+
        jmp     far
:       jsr     sides
        ; the top: bit 4, or (auto) below the eye
        lda     _a3d_pr_vis
        beq     :+
        and     #$10
        beq     @done
        bne     @top
:       lda     _a3d_pr_y1+1
        bpl     @done
@top:   lda     _a3d_pr_shade+4
        jsr     shade
        ldy     #0                  ; clockwise on the screen: 0, 3, 2, 1 (tops)
        ldx     #8
        jsr     putv
        ldy     #2
        ldx     #14
        jsr     putv
        ldy     #4
        ldx     #12
        jsr     putv
        ldy     #6
        ldx     #10
        jsr     putv
        lda     #4
        jsr     _a3d_poly
@done:  lda     #0
        tax
        rts

; the four sides
sides:  lda     #3
        sta     side
@s:     ldx     side
        lda     s_a,x
        sta     ci
        lda     s_b,x
        sta     cj
        lda     _a3d_pr_vis
        beq     @auto
        and     bits,x
        beq     @next
        bne     @draw
@auto:  lda     apex                ; (the pyramid: the fill's own winding test)
        bne     @draw
        jsr     leftof
        bpl     @next
@draw:  ldx     side
        ldy     s_sh,x
        lda     _a3d_pr_shade,y
        jsr     shade
        jsr     quad
@next:  dec     side
        bpl     @s
        rts

.segment "RODATA"
bits:   .byte   1, 2, 4, 8
.segment "CODE"

; far away: one quad from the leftmost to the rightmost corner, shade[0]
far:    ldx     #0                  ; ci = the leftmost, cj = the rightmost
        stx     ci
        stx     cj
        ldx     #6
@f:     stx     kk
        ldy     ci
        lda     _a3d_sx,x
        cmp     _a3d_sx,y
        lda     _a3d_sx+1,x
        sbc     _a3d_sx+1,y
        bvc     :+
        eor     #$80
:       bpl     :+
        stx     ci
:       ldx     kk
        ldy     cj
        lda     _a3d_sx,y
        cmp     _a3d_sx,x
        lda     _a3d_sx+1,y
        sbc     _a3d_sx+1,x
        bvc     :+
        eor     #$80
:       bpl     :+
        stx     cj
:       ldx     kk
        dex
        dex
        bne     @f
        lda     _a3d_pr_shade
        jsr     shade
        lda     #0
        sta     apex
        jsr     quad
        lda     #0
        tax
        rts

; uint8 a3d_pyramid(void): the footprint as a3d_prism's at y0, the apex at y1
; over its middle
_a3d_pyramid:
        lda     #1
        sta     apex
        jsr     footprint
        bcc     :+
        lda     #1
        ldx     #0
        rts
:       clc                         ; the apex: halfway from corner 0 to 2
        lda     _a3d_vx
        adc     _a3d_vx+4
        sta     t0
        lda     _a3d_vx+1
        adc     _a3d_vx+5
        cmp     #$80
        ror     a
        ror     t0
        sta     _a3d_vx+9
        lda     t0
        sta     _a3d_vx+8
        clc
        lda     _a3d_vz
        adc     _a3d_vz+4
        sta     t0
        lda     _a3d_vz+1
        adc     _a3d_vz+5
        cmp     #$80
        ror     a
        ror     t0
        sta     _a3d_vz+9
        lda     t0
        sta     _a3d_vz+8
        lda     #0
        sta     _a3d_xfirst
        lda     #5
        jsr     _a3d_project_
        jsr     sides
        lda     #0
        tax
        rts

; uint8 a3d_wallquad(void): an upright quad from floor points a (a3d_pr_ax,
; az) to b (camera space) between heights y0 and y1, in shade[0], seen from
; either side
_a3d_wallquad:
        ldx     #0
        ldy     #0
        jsr     wpt
        ldx     #2
        ldy     #4
        jsr     wpt
        lda     _a3d_vz+1           ; near?
        ora     _a3d_vz+3
        bmi     @near
        lda     _a3d_vz+1
        bne     :+
        lda     _a3d_vz
        cmp     #NEAR
        bcc     @near
:       lda     _a3d_vz+3
        bne     :+
        lda     _a3d_vz+2
        cmp     #NEAR
        bcc     @near
:       lda     #0
        sta     _a3d_xfirst
        lda     #0
        sta     apex
        lda     #2                  ; vertices 0, 1 (feet) and 4, 5 (tops)
        jsr     _a3d_project_
        lda     #4
        sta     _a3d_xfirst
        lda     #2
        jsr     _a3d_project_
        lda     #0
        sta     ci
        lda     #2
        sta     cj
        jsr     leftof
        bmi     :+
        lda     #2
        sta     ci
        lda     #0
        sta     cj
:       lda     _a3d_pr_shade
        jsr     shade
        jsr     quad
        lda     #0
        tax
        rts
@near:  lda     #1
        ldx     #0
        rts

; floor point a (Y = 0) or b (Y = 4) into vertex X/2 and its top X/2 + 4
wpt:    lda     _a3d_pr_ax,y
        sta     _a3d_vx,x
        sta     _a3d_vx+8,x
        lda     _a3d_pr_ax+1,y
        sta     _a3d_vx+1,x
        sta     _a3d_vx+9,x
        lda     _a3d_pr_az,y
        sta     _a3d_vz,x
        sta     _a3d_vz+8,x
        lda     _a3d_pr_az+1,y
        sta     _a3d_vz+1,x
        sta     _a3d_vz+9,x
        lda     _a3d_pr_y0
        sta     _a3d_vy,x
        lda     _a3d_pr_y0+1
        sta     _a3d_vy+1,x
        lda     _a3d_pr_y1
        sta     _a3d_vy+8,x
        lda     _a3d_pr_y1+1
        sta     _a3d_vy+9,x
        rts
