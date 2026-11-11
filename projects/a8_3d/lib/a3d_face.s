; a3d_face.s - faces through transformed vertices: gather, the back-face
; test, then a3d_poly (a3d_poly.s).

        .importzp a3d_q, a3d_t, a3d_m
        .import   _a3d_px, _a3d_py, _a3d_sx, _a3d_sy, _a3d_vclip, _a3d_poly, _a3d_fn, mulset_s
        .export   _a3d_face_, _a3d_gather_, _a3d_front_

        .include "a3d_span.inc"

.macro  SLT     lo, hi, mlo, mhi
        lda     lo
        cmp     mlo
        lda     hi
        sbc     mhi
        bvc     :+
        eor     #$80
:
.endmacro

.segment "BSS"
fi:     .res 1
fi2:    .res 1

.segment "CODE"
; uint8 a3d_gather_(const uint8 *idx): a3d_fn vertices into a3d_px/py.
; 0 = back-facing, 1 = front, 2 = a vertex is behind the near plane
_a3d_gather_:
        sta     a3d_q
        stx     a3d_q+1
        ldy     #0
        sty     fi2
@g:     lda     (a3d_q),y
        tax
        lda     _a3d_vclip,x
        bne     @nearc
        sty     fi
        txa
        asl     a
        tax
        ldy     fi2
        lda     _a3d_sx,x
        sta     _a3d_px,y
        lda     _a3d_sx+1,x
        sta     _a3d_px+1,y
        lda     _a3d_sy,x
        sta     _a3d_py,y
        lda     _a3d_sy+1,x
        sta     _a3d_py+1,y
        iny
        iny
        sty     fi2
        ldy     fi
        iny
        cpy     _a3d_fn
        bne     @g
        jsr     front
        lda     #0
        tax
        rol     a
        rts
@nearc: lda     #2
        ldx     #0
        rts

; uint8 a3d_front_(void): 1 if a3d_px/py 0, 1, 2 turn clockwise
_a3d_front_:
        jsr     front
        lda     #0
        tax
        rol     a
        rts

; uint8 a3d_face_(const uint8 *idx): gather, cull, draw; 0/1 drawn, 2 = near
_a3d_face_:
        jsr     _a3d_gather_
        cmp     #1
        bne     :+
        lda     _a3d_fn
        jsr     _a3d_poly
        lda     #1
:       ldx     #0
        rts


; carry set if px/py 0, 1, 2 turn clockwise on the screen (cross > 0)
front:
        sec
        lda     _a3d_px+2
        sbc     _a3d_px
        sta     dax
        lda     _a3d_px+3
        sbc     _a3d_px+1
        sta     dax+1
        sec
        lda     _a3d_py+2
        sbc     _a3d_py
        sta     day
        lda     _a3d_py+3
        sbc     _a3d_py+1
        sta     day+1
        sec
        lda     _a3d_px+4
        sbc     _a3d_px
        sta     dbx
        lda     _a3d_px+5
        sbc     _a3d_px+1
        sta     dbx+1
        sec
        lda     _a3d_py+4
        sbc     _a3d_py
        sta     dby
        lda     _a3d_py+5
        sbc     _a3d_py+1
        sta     dby+1
@fit:   ldx     #6                  ; every delta in -128..127?
@f1:    lda     dax,x
        asl     a                   ; carry = sign of the low byte
        lda     dax+1,x
        adc     #0                  ; high + sign: 0 if it fits
        bne     @shrink
        dex
        dex
        bpl     @f1
        ; cross = ax*by - ay*bx
        lda     dax
        jsr     mulset_s
        lda     dby
        eor     #$80
        tay
        sec
        lda     (a3d_m),y
        sbc     (a3d_m+2),y
        sta     dp1
        lda     (a3d_m+4),y
        sbc     (a3d_m+6),y
        sta     dp1+1
        lda     day
        jsr     mulset_s
        lda     dbx
        eor     #$80
        tay
        sec
        lda     (a3d_m),y
        sbc     (a3d_m+2),y
        sta     dax                 ; (reuse) P2
        lda     (a3d_m+4),y
        sbc     (a3d_m+6),y
        sta     dax+1
        ; front if P1 > P2, i.e. P2 < P1
        SLT     dax, dax+1, dp1, dp1+1
        bmi     @yes
        clc
        rts
@yes:   sec
        rts
@shrink:
        ldx     #6
@s1:    lda     dax+1,x
        cmp     #$80
        ror     dax+1,x
        ror     dax,x
        dex
        dex
        bpl     @s1
        jmp     @fit


