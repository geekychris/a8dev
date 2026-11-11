; dlrows.s - the mode D rows of the display list and the colour changes
; (ball.c ball_screen's loop, the C there is the model): for each screen
; row r, the ball's buffer row br = r - ball_row or the row of zeros, and
; a DLI on the entry before whenever the row's colours (the room's, the
; ball's lit three, the shadow's) differ from the last ones set.

        .export _dl_rows
        .export _dr_r, _dr_last, _dr_br, _dr_dx, _dr_rr, _dr_d, _dr_first
        .export _row_lo, _row_hi
        .import _tup, _have, _tab_end, _prev, _vc0, _vc1, _vc2, _room_sh
        .importzp ptr1, ptr2, ptr3, tmp1, tmp2, tmp3, tmp4

TAB_BK  = $AC60
TAB_C0  = TAB_BK + 176
TAB_C1  = TAB_BK + 352
TAB_C2  = TAB_BK + 528
TAB_SH  = TAB_BK + 704
ZERO    = $A000

.segment "DATA"
_dr_r:     .byte 0
_dr_last:  .byte 0
_dr_br:    .byte 0
_dr_dx:    .byte 0
_dr_rr:    .word 0
_dr_d:     .word 0
_dr_first: .byte 0
.segment "BSS"
_row_lo:   .res 64              ; BALL_BUF + br * 40
_row_hi:   .res 64
bk:        .res 1
sh:        .res 1
alo:       .res 1
ahi:       .res 1

.segment "CODE"

_dl_rows:
        lda     _dr_d
        sta     ptr1
        lda     _dr_d+1
        sta     ptr1+1
        lda     _dr_rr
        sta     ptr2
        lda     _dr_rr+1
        sta     ptr2+1
        lda     _prev
        sta     ptr3
        lda     _prev+1
        sta     ptr3+1
@row:   ldy     _dr_r
        cpy     _dr_last
        bcc     @go
        jmp     @done
@go:    lda     (ptr2),y
        sta     bk
        lda     _room_sh,y
        sta     sh
        ldx     _dr_br
        cpx     #64
        bcs     @zero
        ldy     #0              ; dx sign-extended
        lda     _dr_dx
        bpl     @pos
        dey
@pos:   lda     _row_lo,x
        clc
        adc     _dr_dx
        sta     alo
        tya
        adc     _row_hi,x
        sta     ahi
        lda     _vc0,x
        cmp     _tup+1
        bne     @diff0
        lda     _vc1,x
        cmp     _tup+2
        bne     @diff1
        lda     _vc2,x
        cmp     _tup+3
        bne     @diff2
        jmp     @room
@zero:  lda     #<ZERO
        sta     alo
        lda     #>ZERO
        sta     ahi
        jmp     @room
@diff0: sta     _tup+1
        lda     _vc1,x
@diff1: sta     _tup+2
        lda     _vc2,x
@diff2: sta     _tup+3
        jmp     @chg
@room:  lda     _dr_first
        bne     @chg
        lda     bk
        cmp     _tup
        bne     @chg
        lda     sh
        cmp     _tup+4
        beq     @emit
@chg:   lda     bk
        sta     _tup
        lda     sh
        sta     _tup+4
        lda     #0
        sta     _dr_first
        ldx     _have
        cpx     _tab_end
        beq     @emit           ; full (it never is)
        ldy     #0
        lda     (ptr3),y
        ora     #$80
        sta     (ptr3),y
        lda     _tup
        sta     TAB_BK,x
        lda     _tup+1
        sta     TAB_C0,x
        lda     _tup+2
        sta     TAB_C1,x
        lda     _tup+3
        sta     TAB_C2,x
        lda     _tup+4
        sta     TAB_SH,x
        inc     _have
@emit:  lda     ptr1
        sta     ptr3
        lda     ptr1+1
        sta     ptr3+1
        ldy     #0
        lda     #$4D
        sta     (ptr1),y
        iny
        lda     alo
        sta     (ptr1),y
        iny
        lda     ahi
        sta     (ptr1),y
        lda     ptr1
        clc
        adc     #3
        sta     ptr1
        bcc     @n
        inc     ptr1+1
@n:     inc     _dr_r
        inc     _dr_br
        jmp     @row
@done:  lda     ptr1
        sta     _dr_d
        lda     ptr1+1
        sta     _dr_d+1
        lda     ptr3
        sta     _prev
        lda     ptr3+1
        sta     _prev+1
        rts
