; vbi.s - the fine scroll registers (no OS shadows) set in the vertical
; blank, so they change with the display list's addresses; and the DLI
; after the HUD row that switches to the maze's font.
        .export _vbi_install
        .import _view_hscrol, _view_vscrol
HSCROL  = $D404
VSCROL  = $D405
CHBASE  = $D409
WSYNC   = $D40A
NMIEN   = $D40E
VDSLST  = $0200
VVBLKD  = $0224
.segment "DATA"
old:    .word 0
.segment "CODE"
vbi:    lda     _view_hscrol
        sta     HSCROL
        lda     _view_vscrol
        sta     VSCROL
        jmp     (old)
dli:    pha
        lda     #$A0            ; the maze's font
        sta     WSYNC
        sta     CHBASE
        pla
        rti
_vbi_install:
        lda     #0
        sta     NMIEN
        lda     VVBLKD
        sta     old
        lda     VVBLKD+1
        sta     old+1
        lda     #<vbi
        sta     VVBLKD
        lda     #>vbi
        sta     VVBLKD+1
        lda     #<dli
        sta     VDSLST
        lda     #>dli
        sta     VDSLST+1
        lda     #$C0
        sta     NMIEN
        rts
