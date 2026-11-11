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
VIS     = 22                    ; the table's display list lines are VIS + 1 (front.c)
NMIST   = $D40F
        .import os_vbi

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
last_sr: .res 1
nxt:    .res TROWS + 2          ; the next row (> r) whose values differ from the row above
dlion:  .res TROWS + 2 + VIS    ; $80: row r + 1 differs from r (line r's DLI)
t1:     .res 1
t2:     .res 1
old_vbi: .res 2
.segment "HICODE"

; The NMI vector points at dli (disp_start): a DLI goes straight in; the
; vertical blank goes on to os.s's handler. The next row's six values are
; loaded into the DLI's own immediate operands beforehand, so after WSYNC it
; only stores: all six are in by about cycle 20 of the next line, in the
; horizontal blank, before the row's first pixels and font fetches. (Loading
; them from the tables after WSYNC took ~38 cycles: the first line of each
; row showed the old font and colours.)
;
; A row whose font and colours are the row above's needs no DLI: the VBI
; clears the DLI bit of the display list's line above it (dl_bits, when
; the scroll row changes), and nxt[] takes the DLIs from one row that
; changes to the next.
dli:    bit     NMIST
        bmi     @d
        jmp     os_vbi
@d:     pha
        txa
        pha
        tya
        pha
v_bk:   lda     #0
v_pf0:  ldx     #0
v_pf1:  ldy     #0
        sta     WSYNC
        sta     COLBK
        stx     COLPF0
        sty     COLPF1
v_font: lda     #0
        sta     CHBASE
v_pf2:  lda     #0
        sta     COLPF2
v_pf3:  lda     #0
        sta     COLPF3
        ldx     row             ; the next DLI's row: the next that changes
        lda     nxt,x
        sta     row
        jsr     prep
        pla
        tay
        pla
        tax
        pla
        rti

; the values of table row `row` into the DLI's operands
prep:   ldx     row
        lda     _t_bk,x
        sta     v_bk+1
        lda     _t_pf0,x
        sta     v_pf0+1
        lda     _t_pf1,x
        sta     v_pf1+1
        lda     _t_pf2,x
        sta     v_pf2+1
        lda     _t_pf3,x
        sta     v_pf3+1
        lda     _t_font,x
        sta     v_font+1
        rts

vbi:    ldx     _scroll_row
        cpx     last_sr
        beq     @same
        jsr     dl_bits
@same:  lda     _scroll_row
        sta     row
        jsr     prep
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
        jsr     mk_nxt
        lda     #$FF
        sta     last_sr
        lda     #0
        sta     NMIEN
        lda     #<dli
        sta     $FFFA
        lda     #>dli
        sta     $FFFB
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

.segment "TABRO"                ; (HICODE is full: with the table, under the ROM, outside the bank window; it changes its own operands)

; the display list's table lines' DLI bits for scroll row x: line j's
; bit is on when table row x + j + 1 differs from x + j
dl_bits:
        stx     last_sr
        lda     _dl_lms         ; line 0's opcode at dl_lms - 1, line j's at dl_lms + 1 + j
        sec
        sbc     #1
        sta     @o0+1
        lda     _dl_lms+1
        sbc     #0
        sta     @o0+2
        lda     _dl_lms
        clc
        adc     #1
        sta     @oj+1
        lda     _dl_lms+1
        adc     #0
        sta     @oj+2
        lda     dlion,x
        ora     #$64            ; mode 4, LMS, VSCROL
@o0:    sta     $FFFF
        ldy     #1
@j:     inx
        lda     dlion,x
        ora     #$24            ; mode 4, VSCROL
        cpy     #VIS
        bne     @oj
        and     #$80
        ora     #$04            ; the last: no VSCROL
@oj:    sta     $FFFF,y
        iny
        cpy     #VIS + 1
        bne     @j
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
.segment "TABRO"                ; (under the ROM, outside the window: it maps bank 3)
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


.segment "FRONT"                ; (main memory: disp_start runs from the front end)

; nxt[] and dlion[] from the rows' values (row r differs from r - 1: any of
; the six tables)
mk_nxt: ldx     #0
@c:     cpx     #TROWS + 1
        bcs     @no
        lda     _t_bk+1,x
        cmp     _t_bk,x
        bne     @yes
        lda     _t_pf0+1,x
        cmp     _t_pf0,x
        bne     @yes
        lda     _t_pf1+1,x
        cmp     _t_pf1,x
        bne     @yes
        lda     _t_pf2+1,x
        cmp     _t_pf2,x
        bne     @yes
        lda     _t_pf3+1,x
        cmp     _t_pf3,x
        bne     @yes
        lda     _t_font+1,x
        cmp     _t_font,x
        bne     @yes
@no:    lda     #0
        beq     @s
@yes:   lda     #$80
@s:     sta     dlion,x
        inx
        cpx     #TROWS + 2 + VIS
        bne     @c
        ; nxt[r]: from the end down
        ldy     #TROWS + 1      ; (past the table: stays there)
        ldx     #TROWS + 1
@n:     tya
        sta     nxt,x
        dex
        bmi     @done
        lda     dlion,x         ; row x + 1 differs: it is x's next
        beq     @n
        txa
        tay
        iny
        jmp     @n
@done:  rts

