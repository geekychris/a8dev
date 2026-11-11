; foes_draw(): the enemies on players 1-3, by bands of 16 scan lines (the
; level's rows; display.s moves the players at each band). Each enemy takes
; the first player free from its top band down; if none is, it isn't drawn
; this frame. What was drawn last frame is cleared first.
; Enemy: type 0, x 1, y 3, alive 10, frame 11 (13 bytes, at game + 56).
        .export _foes_draw, _foes_clear
        .import _game, _view_x, _foe_gfx, _pend_x1, _pend_c1

ENEMIES = 56
SCAN_TOP = 32
PMG_X0 = 48
PL1 = $9D00                     ; players 1-3: $9D00, $9E00, $9F00

        .zeropage
fp:     .res 2                  ; the enemy
gp:     .res 2                  ; its graphics
dp:     .res 2                  ; where they go
dx:     .res 2
sy:     .res 2
n:      .res 1
b0:     .res 1
b1:     .res 1
pl:     .res 1
hx:     .res 1
col:    .res 1
cnt:    .res 1

        .bss
used:   .res 3                  ; per player: the first band free
clr_lo: .res 16                 ; drawn last frame: where, how many lines
clr_hi: .res 16
clr_n:  .res 16
nclr:   .res 1

        .rodata
colours: .byte $46, $C4, $04, $26     ; beetle, fly, spider, ant

        .code
_foes_clear:
        ldx nclr
        beq @bands
@c:     dex
        lda clr_lo,x
        sta dp
        lda clr_hi,x
        sta dp+1
        ldy clr_n,x
        lda #0
@z:     dey
        sta (dp),y
        cpy #0
        bne @z
        txa
        bne @c
        stx nclr
@bands: lda #0
        ldx #35                 ; pend_x1..3 (36 bytes in a row)
@p:     sta _pend_x1,x
        dex
        bpl @p
        sta used
        sta used+1
        sta used+2
        rts

_foes_draw:
        jsr _foes_clear
        lda #<(_game + ENEMIES)
        sta fp
        lda #>(_game + ENEMIES)
        sta fp+1
        lda #16
        sta cnt
@loop:  ldy #10
        lda (fp),y
        bne :+
        jmp @next
:       ldy #1                  ; dx = x - view_x: -16..319
        lda (fp),y
        sec
        sbc _view_x
        sta dx
        iny
        lda (fp),y
        sbc _view_x+1
        sta dx+1
        lda dx
        clc
        adc #16
        tax
        lda dx+1
        adc #0                  ; dx + 16 in 0..335
        beq @xok
        cmp #1
        bne @skip
        cpx #<336
        bcs @skip
@xok:   iny                     ; sy = y - 16: -15..191
        lda (fp),y
        sec
        sbc #16
        sta sy
        iny
        lda (fp),y
        sbc #0
        sta sy+1
        beq @top
        cmp #$FF
        bne @skip
        lda sy
        cmp #$F1
        bcc @skip
        lda #0                  ; above the view: band 0
        sta b0
        beq @bot
@top:   lda sy
        cmp #192
        bcs @skip
        lsr a
        lsr a
        lsr a
        lsr a
        sta b0
@bot:   lda sy                  ; b1 = (sy + 15) >> 4, at most 11
        clc
        adc #15
        lsr a
        lsr a
        lsr a
        lsr a
        ldx sy+1
        beq :+
        lda #0                  ; (sy < 0: band 0)
:       cmp #12
        bcc :+
        lda #11
:       sta b1
        ldx #0                  ; the first player free at b0
@pl:    lda used,x
        cmp b0
        beq @got
        bcc @got
        inx
        cpx #3
        bne @pl
@skip:  jmp @next
@got:   stx pl
        ldy b1
        iny
        tya
        sta used,x
        lda dx+1                ; hx = PMG_X0 + dx / 2
        cmp #$80
        ror a
        lda dx
        ror a
        clc
        adc #PMG_X0
        sta hx
        ldy #0                  ; k = type - 2
        lda (fp),y
        sec
        sbc #2
        and #3
        tax
        lda colours,x
        sta col
        txa                     ; gp = foe_gfx + (k * 2 + (frame & 1)) * 16
        asl a
        sta n
        ldy #11
        lda (fp),y
        and #1
        ora n
        asl a
        asl a
        asl a
        asl a
        clc
        adc #<_foe_gfx
        sta gp
        lda #>_foe_gfx
        adc #0
        sta gp+1
        lda #16
        sta n
        lda sy                  ; dp = player + SCAN_TOP + sy (or the top)
        ldx sy+1
        beq @dy
        eor #$FF                ; above: skip -sy lines of the graphics
        clc
        adc #1
        pha
        clc
        adc gp
        sta gp
        bcc :+
        inc gp+1
:       pla
        eor #$FF
        sec
        adc #16                 ; n = 16 + sy
        sta n
        lda #0
@dy:    clc
        adc #SCAN_TOP
        sta dp
        lda pl
        clc
        adc #>PL1
        sta dp+1
        ldx nclr                ; to clear next frame
        lda dp
        sta clr_lo,x
        lda dp+1
        sta clr_hi,x
        lda n
        sta clr_n,x
        inc nclr
        ldy n
@cp:    dey
        lda (gp),y
        sta (dp),y
        tya
        bne @cp
        lda pl                  ; pend_x/c[pl][b0..b1]
        asl a
        asl a
        sta n
        asl a
        adc n                   ; pl * 12
        adc b0
        tax
        lda b1
        sec
        sbc b0
        tay
@bd:    lda hx
        sta _pend_x1,x
        lda col
        sta _pend_c1,x
        inx
        dey
        bpl @bd
@next:  lda fp
        clc
        adc #13
        sta fp
        bcc :+
        inc fp+1
:       dec cnt
        beq :+
        jmp @loop
:       rts
