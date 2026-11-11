; Jump Quest - display interrupts.
;
; The level is 12 ANTIC 5 rows that all scroll together: the vertical blank
; writes HSCROL and the rows' LMS addresses (row base + vb_lms) into the
; display list, and the hero's x, from what the game set for this frame
; (vb_ready). The enemies share players 1-3 by bands of 16 scan lines (the
; rows): a display-list interrupt at the end of each row moves the three
; players to their next band's x and colour (bx1..3, bc1..3; band 0's are
; set by the vertical blank).

        .export _display_install, _vb_lms, _vb_hs, _vb_hero, _vb_ready
        .export _row_lo, _row_hi, _dl_rows, _bx1, _bx2, _bx3, _bc1, _bc2, _bc3
        .export _pend_x1, _pend_x2, _pend_x3, _pend_c1, _pend_c2, _pend_c3

WSYNC   = $D40A
HSCROL  = $D404
HPOSP0  = $D000
HPOSP1  = $D001
HPOSP2  = $D002
HPOSP3  = $D003
COLPM1  = $D013
COLPM2  = $D014
COLPM3  = $D015
NMIEN   = $D40E
VDSLST  = $0200
SETVBV  = $E45C
XITVBV  = $E462
NBANDS  = 12

        .bss
_vb_lms:  .res 1
_vb_hs:   .res 1
_vb_hero: .res 1
_vb_ready: .res 1
_row_lo:  .res NBANDS           ; the rows' buffers
_row_hi:  .res NBANDS
_dl_rows: .res 2                ; the first row's LMS operand in the display list
_bx1:     .res NBANDS           ; per band: the players' x and colours (live)
_bx2:     .res NBANDS
_bx3:     .res NBANDS
_bc1:     .res NBANDS
_bc2:     .res NBANDS
_bc3:     .res NBANDS
_pend_x1: .res NBANDS           ; ... as the game set them for the next frame
_pend_x2: .res NBANDS
_pend_x3: .res NBANDS
_pend_c1: .res NBANDS
_pend_c2: .res NBANDS
_pend_c3: .res NBANDS
band:     .res 1
        .zeropage
dp:       .res 2

        .code
.proc _display_install
        lda #0
        sta NMIEN
        lda #<dli
        sta VDSLST
        lda #>dli
        sta VDSLST+1
        lda #7
        ldy #<vbi
        ldx #>vbi
        jsr SETVBV
        lda #$C0
        sta NMIEN
        rts
.endproc

; the next band's players
.proc dli
        pha
        txa
        pha
        inc band
        ldx band
        lda _bx1,x
        sta WSYNC
        sta HPOSP1
        lda _bx2,x
        sta HPOSP2
        lda _bx3,x
        sta HPOSP3
        lda _bc1,x
        sta COLPM1
        lda _bc2,x
        sta COLPM2
        lda _bc3,x
        sta COLPM3
        pla
        tax
        pla
        rti
.endproc

.proc vbi
        lda _vb_ready
        beq bands
        lda _vb_hs
        sta HSCROL
        lda _vb_hero
        sta HPOSP0
        lda _dl_rows            ; LMS operands: 3 bytes apart
        sta dp
        lda _dl_rows+1
        sta dp+1
        ldx #0
        ldy #0
@r:     clc
        lda _row_lo,x
        adc _vb_lms
        sta (dp),y
        iny
        lda _row_hi,x
        adc #0
        sta (dp),y
        iny
        iny
        inx
        cpx #NBANDS
        bne @r
        ldx #NBANDS-1           ; the bands the game made for this frame
@b:     lda _pend_x1,x
        sta _bx1,x
        lda _pend_x2,x
        sta _bx2,x
        lda _pend_x3,x
        sta _bx3,x
        lda _pend_c1,x
        sta _bc1,x
        lda _pend_c2,x
        sta _bc2,x
        lda _pend_c3,x
        sta _bc3,x
        dex
        bpl @b
        lda #0
        sta _vb_ready
bands:  lda #0                  ; band 0
        sta band
        lda _bx1
        sta HPOSP1
        lda _bx2
        sta HPOSP2
        lda _bx3
        sta HPOSP3
        lda _bc1
        sta COLPM1
        lda _bc2
        sta COLPM2
        lda _bc3
        sta COLPM3
        jmp XITVBV
.endproc
