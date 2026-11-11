; a3d_line.s - Bresenham lines in the back buffer (clipping: a3d_line.c).

        .importzp a3d_bsel, a3d_p
        .import   _a3d_ylo, _a3d_yhi, lbyte, dirty_add, ry0, ry1, rb0, rb1
        .export   _a3d_line_, _a3d_lx0, _a3d_ly0, _a3d_lx1, _a3d_ly1

.segment "BSS"
_a3d_lx0:   .res 1
_a3d_ly0:   .res 1
_a3d_lx1:   .res 1
_a3d_ly1:   .res 1
lpat:   .res 1
lmask:  .res 1
ldx_:   .res 1
ldy_:   .res 1
lerr:   .res 1
lcnt:   .res 1
lstep:  .res 1

.segment "RODATA"
pmask:  .byte   $C0, $30, $0C, $03
patc:   .byte   $00, $55, $AA, $FF

.segment "CODE"
; ------------------------------------------------------------------ lines
; void a3d_line_(uint8 c): a3d_lx0,ly0 -> lx1,ly1 inside the 3D area
_a3d_line_:
        and     #3
        tax
        lda     patc,x
        sta     lpat
        ; dirty: the endpoints' box
        lda     _a3d_ly0
        ldx     _a3d_ly1
        cmp     _a3d_ly1
        bcc     :+
        stx     ry0
        sta     ry1
        jmp     :++
:       sta     ry0
        stx     ry1
:       inc     ry1
        ldx     _a3d_lx0
        ldy     _a3d_lx1
        cpx     _a3d_lx1
        bcc     :+
        lda     lbyte,y
        sta     rb0
        lda     lbyte,x
        sta     rb1
        jmp     :++
:       lda     lbyte,x
        sta     rb0
        lda     lbyte,y
        sta     rb1
:       jsr     dirty_add
        ; |dx|, |dy|
        lda     _a3d_lx1
        sec
        sbc     _a3d_lx0
        bcs     :+
        eor     #$FF
        adc     #1
:       sta     ldx_
        lda     _a3d_ly1
        sec
        sbc     _a3d_ly0
        bcs     :+
        eor     #$FF
        adc     #1
:       sta     ldy_
        cmp     ldx_
        bcc     @xmaj
        beq     @xmaj
        jmp     @ymaj
@xmaj:  ; ---- x-major: from the left end
        lda     _a3d_lx1
        cmp     _a3d_lx0
        bcs     :+
        jsr     swap_ends
:       lda     #40                 ; y step +40 or -40
        ldx     _a3d_ly1
        cpx     _a3d_ly0
        bcs     :+
        lda     #<-40
:       sta     lstep
        jsr     line_start
        lda     ldx_
        sta     lcnt
        inc     lcnt                ; dx + 1 pixels
        lsr     a
        sta     lerr
@xl:    lda     (a3d_p),y           ; plot
        eor     lpat
        and     lmask
        eor     (a3d_p),y
        sta     (a3d_p),y
        dec     lcnt
        beq     @xdone
        lda     lerr
        sec
        sbc     ldy_
        bcs     @xnoy
        adc     ldx_
        sta     lerr
        jsr     ystep
        jmp     @xx
@xnoy:  sta     lerr
@xx:    lsr     lmask               ; x + 1
        lsr     lmask
        bne     @xl
        lda     #$C0
        sta     lmask
        iny
        bne     @xl
@xdone: rts
@ymaj:  ; ---- y-major: from the top
        lda     _a3d_ly1
        cmp     _a3d_ly0
        bcs     :+
        jsr     swap_ends
:       lda     #0                  ; x direction: 0 = right, 1 = left
        ldx     _a3d_lx1
        cpx     _a3d_lx0
        bcs     :+
        lda     #1
:       sta     lstep
        jsr     line_start
        lda     ldy_
        sta     lcnt
        inc     lcnt
        lsr     a
        sta     lerr
@yl:    lda     (a3d_p),y
        eor     lpat
        and     lmask
        eor     (a3d_p),y
        sta     (a3d_p),y
        dec     lcnt
        beq     @ydone
        clc                         ; y + 1
        lda     a3d_p
        adc     #40
        sta     a3d_p
        bcc     :+
        inc     a3d_p+1
:       lda     lerr
        sec
        sbc     ldx_
        bcs     @ynox
        adc     ldy_
        sta     lerr
        lda     lstep
        bne     @left
        lsr     lmask
        lsr     lmask
        bne     @yl
        lda     #$C0
        sta     lmask
        iny
        bne     @yl
@left:  asl     lmask
        asl     lmask
        bne     @yl
        lda     #$03
        sta     lmask
        dey
        jmp     @yl
@ynox:  sta     lerr
        jmp     @yl
@ydone: rts

; p += lstep (+-40), keeping Y
ystep:  lda     lstep
        bmi     @m
        clc
        adc     a3d_p
        sta     a3d_p
        bcc     :+
        inc     a3d_p+1
:       rts
@m:     clc
        adc     a3d_p
        sta     a3d_p
        bcs     :+
        dec     a3d_p+1
:       rts

; p = line ly0, Y = lx0 >> 2, lmask = pixel lx0's mask
line_start:
        ldx     _a3d_ly0
        lda     _a3d_ylo,x
        sta     a3d_p
        lda     _a3d_yhi,x
        ora     a3d_bsel
        sta     a3d_p+1
        lda     _a3d_lx0
        and     #3
        tax
        lda     pmask,x
        sta     lmask
        lda     _a3d_lx0
        lsr     a
        lsr     a
        tay
        rts

swap_ends:
        lda     _a3d_lx0
        ldx     _a3d_lx1
        sta     _a3d_lx1
        stx     _a3d_lx0
        lda     _a3d_ly0
        ldx     _a3d_ly1
        sta     _a3d_ly1
        stx     _a3d_ly0
        rts

