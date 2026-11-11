; proj.s - scene.c's projection, without multiplies (view.c called the
; maths' smul16 four times a character a step):
;   sx = (ox + ((x - z) * 14 >> 11)) >> 1      (the Atari's pixels)
;   sy = oy - ((y * 17 + (x + z) * 7) >> 11)    (lines)
; x, y, z Q11 (fix); 24-bit sums.

        .export _proj_xyz, _pj_x, _pj_y, _pj_z, _pj_ox, _pj_oy, _pj_sx, _pj_sy

.segment "SCRBSS"
_pj_x:  .res 2
_pj_y:  .res 2
_pj_z:  .res 2
_pj_ox: .res 2
_pj_oy: .res 2
_pj_sx: .res 2
_pj_sy: .res 2
a0:     .res 3                  ; 24-bit accumulators
a1:     .res 3

.segment "XCODE3"

; a1 = sign-extended 16-bit value in A (lo) / X (hi)
.macro  ld24 dst
        sta     dst
        stx     dst+1
        txa
        and     #$80
        beq     :+
        lda     #$FF
:       sta     dst+2
.endmacro
shl_a0: asl     a0
        rol     a0+1
        rol     a0+2
        rts
shl_a1: asl     a1
        rol     a1+1
        rol     a1+2
        rts
add_a:  clc                     ; a0 += a1
        ldx     #0
        php
@l:     plp
        lda     a0,x
        adc     a1,x
        sta     a0,x
        php
        inx
        cpx     #3
        bne     @l
        plp
        rts
sub_a:  sec                     ; a0 -= a1
        ldx     #0
        php
@l:     plp
        lda     a0,x
        sbc     a1,x
        sta     a0,x
        php
        inx
        cpx     #3
        bne     @l
        plp
        rts
cp_a:   ldx     #2              ; a0 = a1
@l:     lda     a1,x
        sta     a0,x
        dex
        bpl     @l
        rts
asr_a0: lda     a0+2            ; a0 (bytes 1, 2) >> 1, arithmetic
        cmp     #$80
        ror     a0+2
        ror     a0+1
        rts

_proj_xyz:
        ; ---- sx: d = x - z; a0 = d * 7 = d * 8 - d
        sec
        lda     _pj_x
        sbc     _pj_z
        pha
        lda     _pj_x+1
        sbc     _pj_z+1
        tax
        pla
        ld24    a1
        jsr     cp_a
        jsr     shl_a0
        jsr     shl_a0
        jsr     shl_a0
        jsr     sub_a
        ; >> 10: bytes 1..2, then >> 2 (arithmetic)
        jsr     asr_a0
        jsr     asr_a0
        ; + ox, >> 1
        clc
        lda     a0+1
        adc     _pj_ox
        sta     _pj_sx
        lda     a0+2
        adc     _pj_ox+1
        cmp     #$80
        ror
        sta     _pj_sx+1
        ror     _pj_sx
        ; ---- sy: a0 = y * 17 = y * 16 + y
        lda     _pj_y
        ldx     _pj_y+1
        ld24    a1
        jsr     cp_a
        jsr     shl_a0
        jsr     shl_a0
        jsr     shl_a0
        jsr     shl_a0
        jsr     add_a
        ; + (x + z) * 7: a1 = x + z (17 bits, signed), * 8 - itself
        clc
        lda     _pj_x
        adc     _pj_z
        sta     a1
        lda     _pj_x+1
        adc     _pj_z+1
        sta     a1+1
        lda     #0              ; the sign / carry into the third byte
        bvc     :+
        ; overflow: the true sign is the carry's opposite of bit 7
        lda     a1+1
        eor     #$80
        and     #$80
        beq     @pos
        lda     #$FF
        bne     @s
@pos:   lda     #0
        jmp     @s
:       lda     a1+1
        and     #$80
        beq     @s
        lda     #$FF
@s:     sta     a1+2
        jsr     sub_a           ; a0 += 7 * a1 = 8 * a1 - a1
        jsr     shl_a1
        jsr     shl_a1
        jsr     shl_a1
        jsr     add_a
        ; >> 11: (bytes 1..2) >> 3
        jsr     asr_a0
        jsr     asr_a0
        jsr     asr_a0
        ; sy = oy - that
        sec
        lda     _pj_oy
        sbc     a0+1
        sta     _pj_sy
        lda     _pj_oy+1
        sbc     a0+2
        sta     _pj_sy+1
        rts
