; disp.s - Boing Pinball's display interrupts (LOWCODE: they must work while
; a bank is mapped for the physics or the rules).
;
; The display list (front.c): the two text rows of the dot-matrix display,
; then 23 mode 4 rows of the table, scrolled finely (VSCROL) and coarsely
; (the first row's LMS). Every table row has the DLI bit: it sets the next
; row's colours (background, PF0-PF3) and font (CHBASE), from tables per
; table row (gen_gfx.py). The VBI starts the count at the scrolled-to row
; and applies the scroll the game asked for (scroll_row, scroll_fine).

        .export _disp_start, _scroll_row, _scroll_fine, _dl_lms
        .export _t_bk, _t_pf0, _t_pf1, _t_pf2, _t_pf3, _t_font

WSYNC   = $D40A
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
COLPF3  = $D019
COLBK   = $D01A
CHBASE  = $D409
NMIEN   = $D40E
VSCROL  = $D405
VDSLST  = $0200
VVBLKD  = $0224

TROWS   = 48

.segment "HIBSS"
_t_bk:  .res TROWS + 2
_t_pf0: .res TROWS + 2
_t_pf1: .res TROWS + 2
_t_pf2: .res TROWS + 2
_t_pf3: .res TROWS + 2
_t_font: .res TROWS + 2
.segment "HIBSS"
_scroll_row:    .res 1
_scroll_fine:   .res 1
_dl_lms:        .res 2         ; the first table row's LMS operand in the display list
row:    .res 1
t1:     .res 1
t2:     .res 1
old_vbi: .res 2
.segment "HICODE"

dli:    pha
        txa
        pha
        tya
        pha
        ldx     row
        lda     _t_pf1,x
        sta     t1
        lda     _t_font,x
        sta     t2
        lda     _t_bk,x
        ldy     _t_pf0,x
        sta     WSYNC
        sta     COLBK
        sty     COLPF0
        lda     t1
        sta     COLPF1
        lda     _t_pf2,x
        sta     COLPF2
        lda     _t_pf3,x
        sta     COLPF3
        lda     t2
        sta     CHBASE
        inc     row
        pla
        tay
        pla
        tax
        pla
        rti

vbi:    lda     _scroll_row
        sta     row
        lda     _scroll_fine
        sta     VSCROL
        ; the first table row's address: map + scroll_row * 40
        lda     _dl_lms
        sta     @lo+1
        sta     @hi+1
        lda     _dl_lms+1
        sta     @lo+2
        sta     @hi+2
        inc     @hi+1
        bne     @nc
        inc     @hi+2
@nc:    lda     _scroll_row
        asl
        asl
        asl                     ; * 8 (row < 32: no carry)  -- 40 = 8 + 32
        sta     t1
        lda     _scroll_row
        ldx     #0
        stx     t2
        asl
        rol     t2
        asl
        rol     t2
        asl
        rol     t2
        asl
        rol     t2
        asl
        rol     t2              ; t2:A = row * 32
        clc
        adc     t1
        bcc     @n2
        inc     t2
@n2:    clc
        adc     #<MAP
        php
@lo:    sta     $FFFF
        lda     t2
        plp
        adc     #>MAP
@hi:    sta     $FFFF
        jmp     (old_vbi)

MAP     = $8000

_disp_start:
        lda     #0
        sta     NMIEN
        lda     #<dli
        sta     VDSLST
        lda     #>dli
        sta     VDSLST+1
        lda     VVBLKD
        sta     old_vbi
        lda     VVBLKD+1
        sta     old_vbi+1
        lda     #<vbi
        sta     VVBLKD
        lda     #>vbi
        sta     VVBLKD+1
        lda     #$C0
        sta     NMIEN
        rts

; void flip_copy(void): n cells of 8 bytes from bank 3 at fc_src (in the
; window) to the addresses in fc_dst[] (outside it: the fonts at $A000)
        .export _flip_copy, _fc_src, _fc_dst, _fc_n
        .import _bank_select
        .importzp ptr1, ptr2
.segment "HIBSS"
_fc_src: .res 2
_fc_dst: .res 2                ; a table of n destination addresses (outside the window)
_fc_n:  .res 1
fc_k:   .res 1
.segment "HICODE"
_flip_copy:
        lda     #3
        jsr     _bank_select
        lda     _fc_src
        sta     ptr1
        lda     _fc_src+1
        sta     ptr1+1
        lda     _fc_dst
        sta     @d1+1
        sta     @d2+1
        lda     _fc_dst+1
        sta     @d1+2
        sta     @d2+2
        lda     #0
        sta     fc_k
@cell:  lda     fc_k
        asl
        tay
@d1:    lda     $FFFF,y
        sta     ptr2
        iny
@d2:    lda     $FFFF,y
        sta     ptr2+1
        ldy     #7
@b:     lda     (ptr1),y
        sta     (ptr2),y
        dey
        bpl     @b
        lda     ptr1
        clc
        adc     #8
        sta     ptr1
        bcc     @n
        inc     ptr1+1
@n:     inc     fc_k
        lda     fc_k
        cmp     _fc_n
        bne     @cell
        lda     #$FF
        jmp     _bank_select
