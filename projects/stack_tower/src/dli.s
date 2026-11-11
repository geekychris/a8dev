; dli.s - display list interrupts: the twilight sky behind the tower
; (COLBK in bands of 8 mode D lines) and, on the last 3D line, the text
; rows' colours. Stateless: the band comes from VCOUNT.
        .export _st_dli, _st_sky
        .import _a3d_hud_c1, _a3d_hud_c2

VCOUNT = $D40B
WSYNC  = $D40A
COLBK  = $D01A
COLPF1 = $D017
COLPF2 = $D018

.segment "DATA"
; band colours (index (VCOUNT - 16) / 8 after lines 7, 15, ... 79)
_st_sky:
        .byte $80, $82, $72, $74, $64, $56, $56, $48, $48, $4A, $3A, $3A, $3A, $3A

.segment "CODE"
_st_dli:
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
        lda     _st_sky+1,x
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
