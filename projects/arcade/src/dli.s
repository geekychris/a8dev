; Display-list interrupts for the menu: the picture band (ANTIC D) gets its
; own four colours, the text below gets the shadows' colours back.
; VCOUNT tells the two DLIs apart (no state to get out of step).
        .export         _dli_on, _dli_off, _pic_colors

VDSLST  = $0200
NMIEN   = $D40E
WSYNC   = $D40A
VCOUNT  = $D40B
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
COLBK   = $D01A
COLOR0  = $02C4

.bss
_pic_colors:    .res 4          ; background, pixel 1, 2, 3

.code
dli:    pha
        lda     VCOUNT
        cmp     #50
        bcs     @text
        lda     _pic_colors+1
        sta     WSYNC
        sta     COLPF0
        lda     _pic_colors+2
        sta     COLPF1
        lda     _pic_colors+3
        sta     COLPF2
        lda     _pic_colors
        sta     COLBK
        pla
        rti
@text:  lda     COLOR0+1
        sta     WSYNC
        sta     COLPF1
        lda     COLOR0+2
        sta     COLPF2
        lda     COLOR0+4
        sta     COLBK
        pla
        rti

_dli_on:
        lda     #<dli
        sta     VDSLST
        lda     #>dli
        sta     VDSLST+1
        lda     #$C0
        sta     NMIEN
        rts

_dli_off:
        lda     #$40
        sta     NMIEN
        rts
