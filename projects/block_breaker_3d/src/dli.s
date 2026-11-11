; dli.s - display list interrupts: the bricks' rows are bands of lines, and
; each band gets its own COLPF1/COLPF2 (the level's colours for the bricks
; in it); after the bands, the colours for the rest of the tunnel (the
; ring, the shadow); on the last 3D line, the text rows' colours. A count
; of the DLIs, reset by the last one, says which.
        .export _bb_dli, _dli_c1, _dli_c2, _dli_n

WSYNC  = $D40A
COLPF1 = $D017
COLPF2 = $D018
COLBK  = $D01A

.segment "DATA"
_dli_n: .byte   0
; 4 bands (top row first), the rest of the tunnel, the text rows
_dli_c1: .byte  $0E, $0E, $0E, $0E, $BC, $0E
_dli_c2: .byte  $00, $00, $00, $00, $00, $00

.segment "CODE"
_bb_dli:
        pha
        txa
        pha
        tya
        pha
        ldx     _dli_n
        lda     _dli_c1,x
        ldy     _dli_c2,x
        sta     WSYNC
        sta     COLPF1
        sty     COLPF2
        inx
        cpx     #6
        bcc     :+
        lda     #0                      ; the text rows: a black border
        sta     COLBK
        tax
:       stx     _dli_n
        pla
        tay
        pla
        tax
        pla
        rti
