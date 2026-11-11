; Rotoball: the projection's arithmetic (cc65's 32-bit divisions took ~10K
; cycles a point).
;
; recip(z): 2^22 / z for z >= 128 (8.8 units, so 1 / depth), from a table
; of 2^22 / m for the top 8 bits m of z (0.4% at worst). ~100 cycles.
; mulhi(a, b): (a * b) >> 16, unsigned, by four quarter-square 8 x 8
; multiplies (floor.s's tables). ~250 cycles.

        .export _recip, _mulhi, _pobj
        .export _cam_x, _cam_z, _cam_s, _o_wx, _o_wz, _o_vis, _o_sx, _o_g, _o_inv
        .import sqrlo, sqrhi, nsqlo, nsqhi, popax, _off90

        .segment "ZEROPAGE"
ma:     .res 2
mb:     .res 2
p0:     .res 2                          ; partial products
p1:     .res 2
p2:     .res 2
acc:    .res 3
plo:    .res 1
phi:    .res 1

        .bss
_cam_x: .res 2                          ; the pane's camera
_cam_z: .res 2
_cam_s: .res 1                          ; 0: facing +X (P1), 1: -X (P2)
_o_wx:  .res 2                          ; the point
_o_wz:  .res 2
_o_vis: .res 1                          ; out: in front (depth >= 0.5)
_o_sx:  .res 1                          ;      screen x (0..159, clamped)
_o_g:   .res 2                          ;      270 x 256 / depth: the ground's
_o_inv: .res 2                          ;      line below the horizon; 2^22 / depth
xc:     .res 2

        .rodata
rtlo:   .repeat 128, m
        .byte <(4194304 / (m + 128))
        .endrepeat
rthi:   .repeat 128, m
        .byte >(4194304 / (m + 128))
        .endrepeat

        .code
; uint16_t __fastcall__ recip(uint16_t z)
_recip: stx ma+1
        ldy #0                          ; e: the shifts
        cpx #0
        bne @big
        cmp #128
        bcs @look
        lda #128                        ; (the near clip keeps z >= 128)
        bne @look
@big:   sta ma
@sh:    lsr ma+1
        ror ma
        iny
        lda ma+1
        bne @sh
        lda ma
@look:  sec
        sbc #128
        tax
        lda rtlo,x
        sta ma
        lda rthi,x
        cpy #0
        beq @done
@shr:   lsr a
        ror ma
        dey
        bne @shr
@done:  tax
        lda ma
        rts

; the 16-bit product of A x Y (8 x 8) into plo, phi: quarter squares
mul8:   sta sm1+1
        sta sm2+1
        eor #$FF
        sta sm3+1
        sta sm4+1
        sec
sm1:    lda sqrlo,y
sm3:    sbc nsqlo,y
        sta plo
sm2:    lda sqrhi,y
sm4:    sbc nsqhi,y
        sta phi
        rts

; uint16_t __fastcall__ mulhi(uint16_t a, uint16_t b)
_mulhi: sta mb
        stx mb+1
        jsr popax
        sta ma
        stx ma+1
; (ma * mb) >> 16 in A / X
mulhi_zp:
        ; a * b = ah bh << 16 + (ah bl + al bh) << 8 + al bl
        lda ma
        ldy mb
        jsr m_albl                      ; p0 = al * bl
        lda ma+1
        ldy mb
        jsr m_ahbl                      ; p1 = ah * bl
        lda ma
        ldy mb+1
        jsr m_albh                      ; p2 = al * bh
        ; acc = p0 >> 8 + p1 + p2 (17 bits)
        clc
        lda p0+1
        adc p1
        sta acc
        lda p1+1
        adc #0
        sta acc+1
        lda #0
        sta acc+2
        clc
        lda acc
        adc p2
        sta acc
        lda acc+1
        adc p2+1
        sta acc+1
        bcc :+
        inc acc+2
:       lda ma+1                        ; + ah * bh << 8 (into acc >> 8)
        ldy mb+1
        jsr m_ahbh                      ; p0 = ah * bh
        clc
        lda acc+1
        adc p0
        tay
        lda acc+2
        adc p0+1
        tax
        tya
        rts

m_albl: jsr mul8
        lda plo
        sta p0
        lda phi
        sta p0+1
        rts
m_ahbl: jsr mul8
        lda plo
        sta p1
        lda phi
        sta p1+1
        rts
m_albh: jsr mul8
        lda plo
        sta p2
        lda phi
        sta p2+1
        rts
m_ahbh = m_albl

; pobj: project the point o_wx, o_wz for the camera (the original's
; project(): depth = along the facing, across = P1's right is -Z; the
; screen x 80 + 90 across / depth, clamped to the screen)
_pobj:  lda #0
        sta _o_vis
        lda _cam_s
        bne @p2
        sec                             ; P1: depth = wx - cx (wx > cx)
        lda _o_wx
        sbc _cam_x
        tay
        lda _o_wx+1
        sbc _cam_x+1
        tax
        bvc :+
        eor #$80
:       bpl @dep                        ; wx > cx (signed)
        jmp @no
@p2:    sec                             ; P2: depth = cx - wx
        lda _cam_x
        sbc _o_wx
        tay
        lda _cam_x+1
        sbc _o_wx+1
        tax
        bvc :+
        eor #$80
:       bpl @dep
        jmp @no
@dep:   cpx #0                          ; X:Y = depth (unsigned now), >= 128
        bne :+
        cpy #128
        bcs :+
        jmp @no
:       tya
        jsr _recip
        sta _o_inv
        stx _o_inv+1
        sta mb
        stx mb+1
        lda #<1080                      ; 270 x 4
        sta ma
        lda #>1080
        sta ma+1
        jsr mulhi_zp
        sta _o_g
        stx _o_g+1
        ; across: cz - wz (P1), wz - cz (P2)
        lda _cam_s
        bne @x2
        sec
        lda _cam_z
        sbc _o_wz
        sta xc
        lda _cam_z+1
        sbc _o_wz+1
        sta xc+1
        jmp @xa
@x2:    sec
        lda _o_wz
        sbc _cam_z
        sta xc
        lda _o_wz+1
        sbc _cam_z+1
        sta xc+1
@xa:    lda xc                          ; |xc| << 1
        ldx xc+1
        bpl :+
        eor #$FF
        clc
        adc #1
        pha
        txa
        eor #$FF
        adc #0
        tax
        pla
:       asl a
        sta ma
        txa
        rol a
        sta ma+1
        lda _o_inv
        sta mb
        lda _o_inv+1
        sta mb+1
        jsr mulhi_zp                    ; |across| / depth x 128
        cpx #0
        bne @edge
        cmp #114
        bcs @edge
        tay
        lda _off90,y                    ; x 90 / 128
        jmp @side
@edge:  lda #80
@side:  ldx xc+1
        bmi @left
        clc
        adc #80
        cmp #160
        bcc @set
        lda #159
        bne @set
@left:  sta xc
        lda #80
        sec
        sbc xc
        bcs @set
        lda #0
@set:   sta _o_sx
        inc _o_vis
@no:    rts
