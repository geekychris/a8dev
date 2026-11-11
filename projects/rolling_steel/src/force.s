; force.s - phys.c's marble() before the move, in assembly (the same
; arithmetic): the stick's push (5/7 of 34 u/s^2 rolling, all of it on ice,
; 30% in the air; diagonals x 181/256), on the deck the slope's pull and
; the surface's drag, then the trigger boxes (fans and boosts push; acid,
; the finish). On phys.c's B_* scalars; two 1/50 s steps' worth.

        .export _marble_force
        .import _B_x, _B_y, _B_z, _B_vx, _B_vz, _B_gr, _B_cls, _B_sx, _B_sz
        .import _ph_event, _boxes, popa
        .importzp ptr1, tmp1, tmp2, tmp3, tmp4

_meta   = $0400
S_ROUGH = 1
S_ICE   = 2
PE_DISSOLVED = 1
PE_GOAL = 8
BOX_ACID = 0
BOX_GOAL = 1
BOX_ZONE = 2

.segment "BSS"
fjx:    .res 1
fjz:    .res 1
fa:     .res 1                  ; the push's row in the tables
fs:     .res 2
fnb:    .res 1
fk:     .res 2

.segment "RODATA"
; the push a (2 x 56, 2 x 40, 2 x 17) along an axis: +a, -a, and on a
; diagonal (a * 181) >> 8 and (-a * 181) >> 8 (floor)
push_p: .byte 112, 80, 34
push_n: .word 65536 - 112, 65536 - 80, 65536 - 34
diag_p: .byte 79, 56, 24
diag_n: .word 65536 - 80, 65536 - 57, 65536 - 25

.segment "CODE"

; A/X += the push for stick value Y (-1, 0, 1), row fa, diagonal flag in tmp4
addpush:
        cpy     #0
        beq     @r
        sta     tmp1
        stx     tmp2
        ldx     fa
        cpy     #1
        bne     @neg
        lda     tmp4
        bne     @dp
        lda     push_p,x
        jmp     @addp
@dp:    lda     diag_p,x
@addp:  clc
        adc     tmp1
        tay
        lda     tmp2
        adc     #0
        tax
        tya
@r:     rts
@neg:   txa
        asl
        tax
        lda     tmp4
        bne     @dn
        lda     push_n,x
        clc
        adc     tmp1
        tay
        lda     push_n+1,x
        adc     tmp2
        tax
        tya
        rts
@dn:    lda     diag_n,x
        clc
        adc     tmp1
        tay
        lda     diag_n+1,x
        adc     tmp2
        tax
        tya
        rts

; A/X = clamp(A/X, -128, 128)
clamp:  cpx     #0
        beq     @p
        cpx     #$FF
        beq     @n
        txa
        bmi     @lo
@hi:    lda     #128
        ldx     #0
        rts
@lo:    lda     #<(-128)
        ldx     #$FF
        rts
@p:     cmp     #129
        bcs     @hi
        rts
@n:     cmp     #<(-128)
        bcc     @lo
        rts

; A/X = (A/X * k) >> 7, k in Y (72 or 51); |A/X| <= 128
mulk:   sta     fs
        stx     fs+1
        lda     #0
        sta     fk
        sta     fk+1
        tya
        sta     tmp3
        ldy     #8
@b:     lsr     tmp3            ; add fs << bit for each bit of k
        bcc     @n
        lda     fk
        clc
        adc     fs
        sta     fk
        lda     fk+1
        adc     fs+1
        sta     fk+1
@n:     asl     fs
        rol     fs+1
        dey
        bne     @b
        ; >> 7 (arithmetic): (fk << 1) >> 8
        lda     fk
        asl
        lda     fk+1
        rol
        tay                     ; low byte of the result
        ldx     #0
        lda     fk+1
        bpl     @pos
        dex
@pos:   tya
        rts

; A/X = (word at Y) >> n (arithmetic), n in tmp3
asrw:   sta     fs
        stx     fs+1
@s:     lda     fs+1
        cmp     #$80
        ror     fs+1
        ror     fs
        dec     tmp3
        bne     @s
        lda     fs
        ldx     fs+1
        rts

; v (at ptr: _B_vx or _B_vz) -= A/X
.macro  subv v
        sta     tmp1
        stx     tmp2
        lda     v
        sec
        sbc     tmp1
        sta     v
        lda     v+1
        sbc     tmp2
        sta     v+1
.endmacro

; void __fastcall__ marble_force(int8_t jx, int8_t jz)
_marble_force:
        sta     fjz
        jsr     popa
        sta     fjx
        ; the push's row: in the air 2, ice 1? (rolling 1, ice 0)
        ldx     #2
        lda     _B_gr
        beq     @row
        dex
        lda     _B_cls
        cmp     #S_ICE
        bne     @row
        dex
@row:   stx     fa
        lda     #0              ; a diagonal?
        sta     tmp4
        lda     fjx
        beq     @nd
        lda     fjz
        beq     @nd
        inc     tmp4
@nd:    lda     _B_vx
        ldx     _B_vx+1
        ldy     fjx
        jsr     addpush
        sta     _B_vx
        stx     _B_vx+1
        lda     _B_vz
        ldx     _B_vz+1
        ldy     fjz
        jsr     addpush
        sta     _B_vz
        stx     _B_vz+1
        lda     _B_gr
        bne     @deck
        jmp     boxes_
@deck:  ; the slope and the surface
        lda     _B_cls
        cmp     #S_ICE
        bne     @nice
        lda     _B_sx           ; ice: v -= (s * 72) >> 7
        ldx     _B_sx+1
        jsr     clamp
        ldy     #72
        jsr     mulk
        subv    _B_vx
        lda     _B_sz
        ldx     _B_sz+1
        jsr     clamp
        ldy     #72
        jsr     mulk
        subv    _B_vz
        jmp     boxes_
@nice:  ; v -= ((s * 51) >> 7) + (v >> d), d 5 on sand, 8 rolling
        ldy     #8
        cmp     #S_ROUGH
        bne     @d
        ldy     #5
@d:     sty     fnb             ; (d, for now)
        lda     _B_sx
        ldx     _B_sx+1
        jsr     clamp
        ldy     #51
        jsr     mulk
        sta     fk
        stx     fk+1
        lda     fnb
        sta     tmp3
        lda     _B_vx
        ldx     _B_vx+1
        jsr     asrw
        clc
        adc     fk
        pha
        txa
        adc     fk+1
        tax
        pla
        subv    _B_vx
        lda     _B_sz
        ldx     _B_sz+1
        jsr     clamp
        ldy     #51
        jsr     mulk
        sta     fk
        stx     fk+1
        lda     fnb
        sta     tmp3
        lda     _B_vz
        ldx     _B_vz+1
        jsr     asrw
        clc
        adc     fk
        pha
        txa
        adc     fk+1
        tax
        pla
        subv    _B_vz
boxes_: ; the trigger boxes
        lda     _boxes
        sta     ptr1
        lda     _boxes+1
        sta     ptr1+1
        lda     _meta+21        ; MetaHdr.nbox
        sta     fnb
        lda     _B_z            ; k = z >> 7
        asl
        lda     _B_z+1
        rol
        sta     fk
        lda     #0
        rol
        sta     fk+1
@box:   lda     fnb
        bne     @b1
        rts
@b1:    dec     fnb
        ldy     #4              ; y >= y0
        lda     _B_y
        cmp     (ptr1),y
        iny
        lda     _B_y+1
        sbc     (ptr1),y
        bvc     @v1
        eor     #$80
@v1:    bpl     @y1
        jmp     @next
@y1:    ldy     #6              ; y <= y1
        lda     (ptr1),y
        cmp     _B_y
        iny
        lda     (ptr1),y
        sbc     _B_y+1
        bvc     @v2
        eor     #$80
@v2:    bpl     @y2
        jmp     @next
@y2:    ldy     #2              ; r = k - k0 < nrows
        lda     fk
        sec
        sbc     (ptr1),y
        sta     tmp1
        iny
        lda     fk+1
        sbc     (ptr1),y
        bcc     @next
        bne     @next
        ldy     #1
        lda     tmp1
        cmp     (ptr1),y
        bcs     @next
        asl
        asl
        clc
        adc     #10
        tay
        lda     _B_x            ; x0 <= x <= x1
        cmp     (ptr1),y
        iny
        lda     _B_x+1
        sbc     (ptr1),y
        bcc     @next
        iny
        lda     (ptr1),y
        cmp     _B_x
        iny
        lda     (ptr1),y
        sbc     _B_x+1
        bcc     @next
        ; in it
        ldy     #0
        lda     (ptr1),y
        cmp     #BOX_ZONE
        bne     @trig
        ldy     #8              ; v += 2 dv (int8)
        lda     (ptr1),y
        jsr     add2v
        lda     tmp1
        clc
        adc     _B_vx
        sta     _B_vx
        lda     tmp2
        adc     _B_vx+1
        sta     _B_vx+1
        ldy     #9
        lda     (ptr1),y
        jsr     add2v
        lda     tmp1
        clc
        adc     _B_vz
        sta     _B_vz
        lda     tmp2
        adc     _B_vz+1
        sta     _B_vz+1
        jmp     @next
@trig:  ldx     #PE_DISSOLVED
        cmp     #BOX_GOAL
        bne     @ev
        ldx     #PE_GOAL
@ev:    txa
        ora     _ph_event
        sta     _ph_event
@next:  ldy     #1              ; ptr1 += 10 + 4 nrows
        lda     (ptr1),y
        asl
        asl
        clc
        adc     #10
        bcc     @n1
        inc     ptr1+1
        clc
@n1:    adc     ptr1
        sta     ptr1
        bcc     @nb
        inc     ptr1+1
@nb:    jmp     @box

; tmp2:tmp1 = 2 * A (int8, sign-extended)
add2v:  ldx     #0
        cmp     #$80
        bcc     @p
        dex
@p:     stx     tmp2
        asl
        sta     tmp1
        rol     tmp2
        rts
