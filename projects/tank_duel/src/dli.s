; dli.s - the floor's colour from the horizon down (COLBK), then the text
; rows' colours on the last 3D line (a3d's). By VCOUNT.
        .export _td_dli, _sky_col, _floor_col
        .import _a3d_hud_c1, _a3d_hud_c2

VCOUNT = $D40B
WSYNC  = $D40A
COLBK  = $D01A
COLPF1 = $D017
COLPF2 = $D018

.segment "DATA"
_sky_col:   .byte $86
_floor_col: .byte $C2

.segment "CODE"
_td_dli:
        pha
        lda     VCOUNT
        cmp     #80
        bcs     @hud
        lda     _floor_col
        sta     WSYNC
        sta     COLBK
        pla
        rti
@hud:   txa
        pha
        lda     _a3d_hud_c2
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
