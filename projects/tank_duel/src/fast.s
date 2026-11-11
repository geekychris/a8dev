; fast.s - the hot maths in asm (cc65 spent 1,000 cycles on td_len and
; 3,400 on a corner's rotation):
;
;   uint16_t __fastcall__ td_len_(void)   |(ln_x, ln_z)| within 4%:
;                       max - max/32 + min/4 + min/8 + min/64 (td_fix.c's)
;   void rot2(void)     (r_dx, r_dz) world units -> camera: r_cx = dx*cc -
;                       dz*cs, r_cz = dx*cs + dz*cc, >> 8 (cs, cc Q8, |..| <= 256)

        .export _td_len_, _ln_x, _ln_z
        .export _rot2, _r_dx, _r_dz, _r_cx, _r_cz, _r_cs, _r_cc

.segment "BSS"
_ln_x:  .res 2
_ln_z:  .res 2
mx:     .res 2
mn:     .res 2
acc:    .res 2
_r_dx:  .res 2
_r_dz:  .res 2
_r_cx:  .res 2
_r_cz:  .res 2
_r_cs:  .res 2                  ; Q8 (-256..256)
_r_cc:  .res 2
ma:     .res 2                  ; mul: 16-bit a, 9-bit signed b
mb:     .res 2
mr:     .res 3
sgn:    .res 1

.segment "CODE"

; |v| of the word at X (in BSS: _ln_x / _ln_z) -> A (lo) / Y (hi)
abs16:  lda     1,x
        bpl     @p
        sec
        lda     #0
        sbc     0,x
        pha
        lda     #0
        sbc     1,x
        tay
        pla
        rts
@p:     ldy     1,x
        lda     0,x
        rts

_td_len_:
        ldx     #<_ln_x
        lda     _ln_x+1         ; |x|
        bpl     @xp
        sec
        lda     #0
        sbc     _ln_x
        sta     mx
        lda     #0
        sbc     _ln_x+1
        sta     mx+1
        jmp     @z
@xp:    sta     mx+1
        lda     _ln_x
        sta     mx
@z:     lda     _ln_z+1         ; |z|
        bpl     @zp
        sec
        lda     #0
        sbc     _ln_z
        sta     mn
        lda     #0
        sbc     _ln_z+1
        sta     mn+1
        jmp     @order
@zp:    sta     mn+1
        lda     _ln_z
        sta     mn
@order: lda     mx              ; mx >= mn
        cmp     mn
        lda     mx+1
        sbc     mn+1
        bcs     @ok
        ldx     mx
        ldy     mx+1
        lda     mn
        sta     mx
        lda     mn+1
        sta     mx+1
        stx     mn
        sty     mn+1
@ok:    ; acc = mx - mx/32
        lda     mx+1
        sta     acc+1
        lda     mx
        sta     acc
        lsr     acc+1           ; mx / 32 into mr (5 shifts)
        ror     acc
        lsr     acc+1
        ror     acc
        lsr     acc+1
        ror     acc
        lsr     acc+1
        ror     acc
        lsr     acc+1
        ror     acc
        sec
        lda     mx
        sbc     acc
        sta     mr
        lda     mx+1
        sbc     acc+1
        sta     mr+1
        ; + mn/4 + mn/8 + mn/64
        lsr     mn+1
        ror     mn
        lsr     mn+1
        ror     mn              ; mn/4
        clc
        lda     mr
        adc     mn
        sta     mr
        lda     mr+1
        adc     mn+1
        sta     mr+1
        lsr     mn+1
        ror     mn              ; mn/8
        clc
        lda     mr
        adc     mn
        sta     mr
        lda     mr+1
        adc     mn+1
        sta     mr+1
        lsr     mn+1
        ror     mn
        lsr     mn+1
        ror     mn
        lsr     mn+1
        ror     mn              ; mn/64
        clc
        lda     mr
        adc     mn
        ldx     mr+1
        bcc     :+
        inx
:       rts

; mr = (ma * mb) >> 8, ma int16, mb Q8 in -256..256 (signed). Shift-add on
; magnitudes: 9 rounds.
mul16q8:
        lda     #0
        sta     sgn
        lda     ma+1
        bpl     :+
        jsr     neg_ma
        inc     sgn
:       lda     mb+1
        bpl     :+
        sec
        lda     #0
        sbc     mb
        sta     mb
        lda     #0
        sbc     mb+1
        sta     mb+1
        inc     sgn
:       lda     #0
        sta     mr
        sta     mr+1
        sta     mr+2
        ldx     #9              ; mb's 9 bits (256 = $100)
@l:     lsr     mb+1
        ror     mb
        bcc     @n
        clc
        lda     mr+1
        adc     ma
        sta     mr+1
        lda     mr+2
        adc     ma+1
        sta     mr+2
@n:     ror     mr+2
        ror     mr+1
        ror     mr
        dex
        bne     @l
        ; after 9 rounds the product is mr (24 bits) << ... : it is P >> 9 in
        ; mr+1..2 with bit 8 in mr's top: P >> 8 = (mr+1..2) << 1 | mr bit 7
        asl     mr
        rol     mr+1
        rol     mr+2
        lda     sgn
        and     #1
        beq     @pos
        sec
        lda     #0
        sbc     mr+1
        sta     mr+1
        lda     #0
        sbc     mr+2
        sta     mr+2
@pos:   rts
neg_ma: sec
        lda     #0
        sbc     ma
        sta     ma
        lda     #0
        sbc     ma+1
        sta     ma+1
        rts

; t = (a * b) >> 8 into (mr+1, mr+2): macro-ish helper
.macro  mul aa, bb
        lda     aa
        sta     ma
        lda     aa+1
        sta     ma+1
        lda     bb
        sta     mb
        lda     bb+1
        sta     mb+1
        jsr     mul16q8
.endmacro

_rot2:  mul     _r_dx, _r_cc
        lda     mr+1
        sta     _r_cx
        lda     mr+2
        sta     _r_cx+1
        mul     _r_dz, _r_cs
        sec
        lda     _r_cx
        sbc     mr+1
        sta     _r_cx
        lda     _r_cx+1
        sbc     mr+2
        sta     _r_cx+1
        mul     _r_dx, _r_cs
        lda     mr+1
        sta     _r_cz
        lda     mr+2
        sta     _r_cz+1
        mul     _r_dz, _r_cc
        clc
        lda     _r_cz
        adc     mr+1
        sta     _r_cz
        lda     _r_cz+1
        adc     mr+2
        sta     _r_cz+1
        rts
