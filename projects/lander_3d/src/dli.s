; dli.s - the sky in bands above the horizon (COLBK, one band a drawn
; line), and the text rows' colours on the last 3D line. By VCOUNT.
        .export _ld_dli
        .import _view_sky, _a3d_hud_c1, _a3d_hud_c2

VCOUNT = $D40B
WSYNC  = $D40A
COLBK  = $D01A
COLPF1 = $D017
COLPF2 = $D018

.segment "CODE"
_ld_dli:
        pha
        txa
        pha
        lda     VCOUNT
        cmp     #90
        bcs     @hud
        sec                         ; drawn line y ends at VCOUNT 17 + 2y
        sbc     #16
        lsr     a
        cmp     #6
        bcc     :+
        lda     #6
:       tax
        lda     _view_sky+1,x
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
