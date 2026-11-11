; Rotoball: a frame's floor - each floor line of a pane's display list
; pointed at its row's stripes for the camera (see floor.c).
;
; For row r: the cell along the pitch, floor(cx / 5 +/- 54 / k) (fu_cx
; +/- row_d: fu_sub set for P2, facing -X), odd or even; the phase across, frac(F + frac(-24 / k)) (fu_f +
; row_t8), plus half a period (128) when the cell's parity (with fu_flip)
; says so; the start pixel = phase x period / 256 (a quarter-square
; multiply); copy start & 3, byte start / 4. The address goes into the
; line's LMS (offsets fu_o1[4 r], fu_o2[4 r] in the pane's list: a row
; of one line has the same offset twice; a row hidden by text the last
; row's, which is written last).
; The row tables are spaced 4 apart (X = 4 r; ra_lo / ra_hi at 4 r + copy).
; ~150 cycles a row, 31 rows a pane.

        .export _floor_update, _fu_dl, _fu_o1, _fu_o2, _fu_cx, _fu_f, _fu_flip, _fu_sub
        .import _row_p8, _row_t8, _row_dlo, _row_dhi, _ra_lo, _ra_hi

FROWS   = 31

        .segment "ZEROPAGE"
_fu_dl: .res 2                          ; the pane's first display list byte
tb:     .res 1
tr:     .res 1

        .bss
_fu_o1: .res 2                          ; its rows' LMS offsets (4 r)
_fu_o2: .res 2
_fu_cx:   .res 2                        ; cx / 5 (8.8 cells)
_fu_sub:  .res 1                        ; 1: cx / 5 - 54 / k (P2)
_fu_f:    .res 1                        ; the camera's phase across (1/256)
_fu_flip: .res 1                        ; $80 or 0

        .segment "ALIGN1K"
; the quarter squares: sqr(n) = n * n / 4, nsq(n) = (n - 255)^2 / 4
sqrlo:  .repeat 512, i
        .byte <((i * i) / 4)
        .endrepeat
sqrhi:  .repeat 512, i
        .byte >((i * i) / 4)
        .endrepeat
nsqlo:  .repeat 512, i
        .byte <(((i - 255) * (i - 255)) / 4)
        .endrepeat
nsqhi:  .repeat 512, i
        .byte >(((i - 255) * (i - 255)) / 4)
        .endrepeat
        .export sqrlo, sqrhi, nsqlo, nsqhi

        .code
_floor_update:
        lda _fu_o1                      ; the pane's offset tables (self-modified)
        sta so1a+1
        sta so1b+1
        lda _fu_o1+1
        sta so1a+2
        sta so1b+2
        lda _fu_o2
        sta so2a+1
        sta so2b+1
        lda _fu_o2+1
        sta so2a+2
        sta so2b+2
        lda #$18                        ; clc, adc abs,x (P1) / sec, sbc abs,x
        ldy #$7D
        ldx _fu_sub
        beq :+
        lda #$38
        ldy #$FD
:       sta cc
        sty op1
        sty op2
        ldx #0
f_row:  stx tr
cc:     clc                             ; the cell along the pitch: parity
        lda _fu_cx
op1:    adc _row_dlo,x
        lda _fu_cx+1
op2:    adc _row_dhi,x
        lsr a                           ; C = parity
        lda #0
        ror a                           ; $80 when odd
        eor _fu_flip
        clc
        adc _row_t8,x
        clc
        adc _fu_f                       ; the phase
        ldy _row_p8,x                   ; x the period: the start pixel
        sta sm1+1
        sta sm3+1
        eor #$FF
        sta sm2+1
        sta sm4+1
        sec
sm1:    lda sqrlo,y
sm2:    sbc nsqlo,y
sm3:    lda sqrhi,y
sm4:    sbc nsqhi,y
        tay
        and #3                          ; the copy
        ora tr
        sta tb                          ; 4 r + copy
        tya
        lsr a
        lsr a                           ; the byte
        ldy tb
        clc
        adc _ra_lo,y
so1a:   ldy $FFFF,x                     ; both LMS (a single row's twice)
        sta (_fu_dl),y
so2a:   ldy $FFFF,x
        sta (_fu_dl),y
        ldy tb
        lda _ra_hi,y
        adc #0
so1b:   ldy $FFFF,x
        iny
        sta (_fu_dl),y
so2b:   ldy $FFFF,x
        iny
        sta (_fu_dl),y
        inx
        inx
        inx
        inx
        cpx #FROWS * 4
        bcs :+
        jmp f_row
:       rts
