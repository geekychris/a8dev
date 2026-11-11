; fill_buf(base) / fill_want(base): 256 bytes of times7[j] + base into buf / want
; (~16 cycles a byte; the same loop in cc65 C took ~100).
        .export         _fill_buf, _fill_want
        .import         _times7, _buf, _want
        .importzp       tmp1

.code
_fill_buf:
        sta     tmp1
        ldy     #0
@l:     lda     _times7,y
        clc
        adc     tmp1
        sta     _buf,y
        iny
        bne     @l
        rts

_fill_want:
        sta     tmp1
        ldy     #0
@l:     lda     _times7,y
        clc
        adc     tmp1
        sta     _want,y
        iny
        bne     @l
        rts
