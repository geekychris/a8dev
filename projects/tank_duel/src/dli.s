; dli.s - display list interrupts: the sky's bands (lighter towards the
; horizon), the floor's bands, then the text rows' colours. Nothing in the
; view draws the sky or the floor: they are the background colour, set
; here line by line. A count of the DLIs (reset by the last one and on the
; first line) says which.
        .export _td_dli, _dli_bk, _dli_n
        .import _a3d_hud_c1, _a3d_hud_c2

WSYNC  = $D40A
COLPF1 = $D017
COLPF2 = $D018
COLBK  = $D01A
VCOUNT = $D40B
NBAND  = 7

.segment "DATA"
_dli_n: .byte   0
; after lines 9, 19, 29: the sky; 37 (the horizon), 44, 54, 68: the floor
_dli_bk: .byte  $98, $9A, $9C, $C2, $C4, $C4, $C6

.segment "CODE"
_td_dli:
        pha
        txa
        pha
        ldx     _dli_n
        lda     VCOUNT              ; the first of the frame (resyncs the count)
        cmp     #30
        bcs     :+
        ldx     #0
:       cpx     #NBAND
        bcs     @hud
        lda     _dli_bk,x
        sta     WSYNC
        sta     COLBK
        inx
        stx     _dli_n
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
        sta     _dli_n
        pla
        tax
        pla
        rti
