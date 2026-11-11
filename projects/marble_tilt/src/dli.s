; dli.s - display list interrupts: the void under the board
; (COLBK in bands of 8 mode D lines) and, on the last 3D line, the text
; rows' colours. Stateless: the band comes from VCOUNT.
        .export _mt_dli, _mt_sky
        .import _a3d_hud_c1, _a3d_hud_c2

VCOUNT = $D40B
WSYNC  = $D40A
COLBK  = $D01A
COLPF1 = $D017
COLPF2 = $D018

.segment "DATA"
; band colours (index (VCOUNT - 16) / 8 after lines 7, 15, ... 79)
_mt_sky:
        .byte $00, $00, $90, $90, $92, $92, $82, $82, $72, $72, $62, $62, $62, $62

.segment "CODE"
_mt_dli:
        pha
        txa
        pha
        lda     VCOUNT
        cmp     #100
        bcs     @hud
        sec
        sbc     #16
        lsr     a
        lsr     a
        lsr     a
        tax
        lda     _mt_sky+1,x
        sta     WSYNC
        sta     COLBK
        pla
        tax
        pla
        rti
@hud:   lda     _a3d_hud_c2
        ldx     _a3d_hud_c1
        sta     WSYNC
        sta     COLPF2
        stx     COLPF1
        lda     #0
        sta     COLBK
        pla
        tax
        pla
        rti
