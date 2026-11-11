; Warden Keep: draw a shape into the screen (ANTIC E, 40 bytes a line)
; through its mask: 4 bytes a line, each a (mask, data) pair:
;   screen = (screen & ~mask) | data
; blit_dst: the screen byte of the shape's top left, blit_src: its
; (mask, data) pairs, blit_h: its lines.

        .export _blit, _blit_dst, _blit_src, _blit_h
        .importzp ptr1, ptr2

        .segment "BSS"
_blit_dst: .res 2
_blit_src: .res 2
_blit_h:   .res 1

        .segment "CODE"
_blit:  lda _blit_dst
        sta ptr1
        lda _blit_dst+1
        sta ptr1+1
        lda _blit_src
        sta ptr2
        lda _blit_src+1
        sta ptr2+1
        ldx _blit_h
        beq done
line:
        ldy #0
        lda (ptr2),y            ; mask 0
        eor #$FF
        and (ptr1),y
        ldy #1
        ora (ptr2),y            ; data 0
        dey
        sta (ptr1),y
        ldy #2
        lda (ptr2),y
        eor #$FF
        ldy #1
        and (ptr1),y
        ldy #3
        ora (ptr2),y
        ldy #1
        sta (ptr1),y
        ldy #4
        lda (ptr2),y
        eor #$FF
        ldy #2
        and (ptr1),y
        ldy #5
        ora (ptr2),y
        ldy #2
        sta (ptr1),y
        ldy #6
        lda (ptr2),y
        eor #$FF
        ldy #3
        and (ptr1),y
        ldy #7
        ora (ptr2),y
        ldy #3
        sta (ptr1),y
        clc                     ; next line: src + 8, dst + 40
        lda ptr2
        adc #8
        sta ptr2
        bcc :+
        inc ptr2+1
:       clc
        lda ptr1
        adc #40
        sta ptr1
        bcc :+
        inc ptr1+1
:       dex
        bne line
done:   rts

; restore: 4 bytes a line, restore_h lines, from bg + restore_off to the
; screen ($A010) + restore_off (the background under a moved object)
        .export _restore, _restore_off, _restore_h
        .import _bg

        .segment "BSS"
_restore_off: .res 2
_restore_h:   .res 1

        .segment "CODE"
_restore:
        clc
        lda _restore_off
        adc #<_bg
        sta ptr2
        lda _restore_off+1
        adc #>_bg
        sta ptr2+1
        clc
        lda _restore_off
        adc #<$A010
        sta ptr1
        lda _restore_off+1
        adc #>$A010
        sta ptr1+1
        ldx _restore_h
        beq rdone
rline:  ldy #3
:       lda (ptr2),y
        sta (ptr1),y
        dey
        bpl :-
        clc
        lda ptr2
        adc #40
        sta ptr2
        bcc :+
        inc ptr2+1
:       clc
        lda ptr1
        adc #40
        sta ptr1
        bcc :+
        inc ptr1+1
:       dex
        bne rline
rdone:  rts
