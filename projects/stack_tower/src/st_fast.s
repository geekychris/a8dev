; st_fast.s - the rules' innermost primitives in asm (port: the original's
; st_cells_of / st_collides / st_ghost_row, same results; the browser
; vectors check it). cc65's C spent most of its time in runtime multiply
; and shift helpers here.
;
;   uint8 st_cells(const Piece *p)     -> st_cx[], st_cy[] (columns wrapped), count
;   uint8 st_collides(const Piece *p)  -> 1 if a cell is off the board or taken
;   int8  st_ghost_row(void)           -> the row the falling piece would land on

        .importzp a3d_t
        .import   _game, _st_rot_cells, _st_ncells
        .export   _st_cellsp, _st_collides, _st_ghost_row, _st_cx, _st_cy

BOARD   = _game + 9             ; offsetof(StGame, board) (checked in st_core.c)
PIECE   = _game + 420           ; offsetof(StGame, piece)
TOTAL   = 24

ptr     = a3d_t+0
cp      = a3d_t+2
tcol    = a3d_t+4
trow    = a3d_t+5
n       = a3d_t+6
best    = a3d_t+7
gap     = a3d_t+8
i       = a3d_t+9

.segment "BSS"
_st_cx: .res 5
_st_cy: .res 5

.segment "RODATA"
rowlo:  .repeat TOTAL, I
        .byte   <(BOARD + I * 16)
        .endrepeat
rowhi:  .repeat TOTAL, I
        .byte   >(BOARD + I * 16)
        .endrepeat

.segment "CODE"

; uint8 st_cells(const Piece *p): A/X = p
_st_cellsp:
        sta     ptr
        stx     ptr+1
        ldy     #0
        lda     (ptr),y             ; type
        tax
        lda     _st_ncells,x
        sta     n
        txa                         ; cp = rot_cells + type*40 + (rot&3)*10
        asl     a
        asl     a
        asl     a                   ; type*8 (< 64)
        sta     cp
        asl     a
        asl     a                   ; type*32 (< 256)
        clc
        adc     cp
        sta     cp                  ; type*40 (< 320: carry into high below)
        lda     #0
        rol     a
        sta     cp+1
        ldy     #1
        lda     (ptr),y
        and     #3
        sta     i
        asl     a
        asl     a
        adc     i                   ; rot*5
        asl     a                   ; rot*10
        clc
        adc     cp
        sta     cp
        lda     cp+1
        adc     #0
        sta     cp+1
        clc
        lda     cp
        adc     #<_st_rot_cells
        sta     cp
        lda     cp+1
        adc     #>_st_rot_cells
        sta     cp+1
        ldy     #2
        lda     (ptr),y
        sta     tcol
        iny
        lda     (ptr),y
        sta     trow
        ldx     #0
        ldy     #0
@l:     lda     (cp),y
        clc
        adc     tcol
        and     #15
        sta     _st_cx,x
        iny
        lda     (cp),y
        clc
        adc     trow
        sta     _st_cy,x
        iny
        inx
        cpx     n
        bne     @l
        lda     n
        ldx     #0
        rts

; uint8 st_collides(const Piece *p)
_st_collides:
        jsr     _st_cellsp
        ldx     #0
@c:     ldy     _st_cy,x
        bmi     @yes                ; row < 0
        cpy     #TOTAL
        bcs     @yes
        lda     rowlo,y
        sta     ptr
        lda     rowhi,y
        sta     ptr+1
        ldy     _st_cx,x
        lda     (ptr),y
        bne     @yes
        inx
        cpx     n
        bne     @c
        lda     #0
        tax
        rts
@yes:   lda     #1
        ldx     #0
        rts

; int8 st_ghost_row(void): the smallest gap under any cell of the piece
_st_ghost_row:
        lda     #<PIECE
        ldx     #>PIECE
        jsr     _st_cellsp
        lda     #127
        sta     best
        ldx     #0
@cell:  stx     i
        lda     #0
        sta     gap
        ldy     _st_cy,x
        dey                         ; the row under the cell
        lda     _st_cx,x
        sta     tcol
@down:  tya
        bmi     @stop               ; below row 0: the floor
        cpy     #TOTAL
        bcs     @free               ; above the board: free
        lda     rowlo,y
        sta     ptr
        lda     rowhi,y
        sta     ptr+1
        sty     trow
        ldy     tcol
        lda     (ptr),y
        ldy     trow
        cmp     #0
        bne     @stop
@free:  inc     gap
        dey
        jmp     @down
@stop:  lda     gap
        cmp     best
        bcs     :+
        sta     best
:       ldx     i
        inx
        cpx     n
        bne     @cell
        lda     PIECE+3             ; row - best
        sec
        sbc     best
        ldx     #0
        cmp     #$80
        bcc     :+
        dex
:       rts
