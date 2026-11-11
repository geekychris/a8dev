; pushout.s - td_core.c's push_out() in asm: a tank out of every obstacle
; it overlaps (in order: each asked from where the last push left it),
; then inside the arena's walls.
;   crate: out along the axis of the smaller overlap;
;   pillar, pyramid: d = |(dx, dz)|, if d < reach: (min 16; then dx = 16)
;     the tank to the obstacle + (dx, dz) * reach / d.
; reach = (r + 28) * 16. drive.s's push_out_fast comes here when an
; obstacle's box is near.
;
;   void __fastcall__ push_out_(Tank *t)
;
; Tank: x +3, z +5. Obstacle (7 bytes): kind, hp, x, z, r.

        .macpack longbranch
        .export _push_out_
        .import _obs_next, _oq_x, _oq_z, _oq_extra, _oq_from, _obs_base
        .import _td_len_, _ln_x, _ln_z, _a3d_muldiv_, _a3d_mda, _a3d_mdb

TANK_R  = 28
LIM     = 16 * (1000 - TANK_R)
CRATE   = 2

.segment "ZEROPAGE"
pt:     .res 2                          ; the tank
po:     .res 2                          ; the obstacle

.segment "BSS"
reach:  .res 2
dx:     .res 2
dz:     .res 2
dd:     .res 2
px:     .res 2
pz:     .res 2

.segment "HI1CODE"

_push_out_:
        sta     pt
        stx     pt+1
        lda     #TANK_R
        sta     _oq_extra
        lda     #0
        sta     _oq_from
ask:    ldy     #3
        lda     (pt),y
        sta     _oq_x
        iny
        lda     (pt),y
        sta     _oq_x+1
        iny
        lda     (pt),y
        sta     _oq_z
        iny
        lda     (pt),y
        sta     _oq_z+1
        jsr     _obs_next
        cmp     #$FF
        jeq     walls
        tax
        inx
        stx     _oq_from
        ; po = obs_base + k * 7
        sta     po
        asl
        asl
        asl                             ; k * 8 (k < 16)
        sec
        sbc     po                      ; k * 7
        clc
        adc     _obs_base
        sta     po
        lda     _obs_base+1
        adc     #0
        sta     po+1
        ; reach = (r + 28) * 16
        ldy     #6
        lda     (po),y
        clc
        adc     #TANK_R
        sta     reach
        lda     #0
        rol
        ldx     #4
:       asl     reach
        rol
        dex
        bne     :-
        sta     reach+1
        ; dx = t.x - o.x, dz = t.z - o.z
        ldy     #3
        sec
        lda     (pt),y
        ldy     #2
        sbc     (po),y
        sta     dx
        ldy     #4
        lda     (pt),y
        ldy     #3
        sbc     (po),y
        sta     dx+1
        ldy     #5
        sec
        lda     (pt),y
        ldy     #4
        sbc     (po),y
        sta     dz
        ldy     #6
        lda     (pt),y
        ldy     #5
        sbc     (po),y
        sta     dz+1
        ldy     #0
        lda     (po),y
        cmp     #CRATE
        jne     round_
        ; a crate: pz = reach - |dz|, px = reach - |dx|
        lda     dz
        ldx     dz+1
        jsr     abs_
        sec
        lda     reach
        sbc     px
        sta     pz
        lda     reach+1
        sbc     px+1
        sta     pz+1
        lda     dx
        ldx     dx+1
        jsr     abs_
        sec
        lda     reach
        sbc     px
        sta     px
        lda     reach+1
        sbc     px+1
        sta     px+1
        ; px < pz (signed) ?
        sec
        lda     px
        sbc     pz
        lda     px+1
        sbc     pz+1
        bvc     :+
        eor     #$80
:       bpl     @zax
        ; t.x += dx < 0 ? -px : px
        ldx     #3
        lda     dx+1
        bpl     @add
        jsr     negpx
        jmp     @add
@zax:   lda     pz
        sta     px
        lda     pz+1
        sta     px+1
        ldx     #5
        lda     dz+1
        bpl     @add
        jsr     negpx
@add:   txa
        tay
        clc
        lda     (pt),y
        adc     px
        sta     (pt),y
        iny
        lda     (pt),y
        adc     px+1
        sta     (pt),y
        jmp     ask

; |A/X| into px
abs_:   sta     px
        stx     px+1
        cpx     #$80
        bcc     negok
negpx:  sec
        lda     #0
        sbc     px
        sta     px
        lda     #0
        sbc     px+1
        sta     px+1
negok:  rts

; a pillar or a pyramid: round
round_: lda     dx
        sta     _ln_x
        lda     dx+1
        sta     _ln_x+1
        lda     dz
        sta     _ln_z
        lda     dz+1
        sta     _ln_z+1
        jsr     _td_len_
        sta     dd
        stx     dd+1
        cmp     reach                   ; d >= reach (unsigned): clear of it
        txa
        sbc     reach+1
        jcs     ask
        lda     dd+1                    ; d < 16: d = dx = 16
        bne     :+
        lda     dd
        cmp     #16
        bcs     :+
        lda     #16
        sta     dd
        sta     dx
        lda     #0
        sta     dx+1
:       lda     dx
        ldx     dx+1
        ldy     #3
        jsr     out1
        lda     dz
        ldx     dz+1
        ldy     #5
        jsr     out1
        jmp     ask

; t[y] = o[y - 1] + (A/X) * reach / d
out1:   sta     _a3d_mda
        stx     _a3d_mda+1
        sty     yy
        lda     reach
        sta     _a3d_mdb
        lda     reach+1
        sta     _a3d_mdb+1
        lda     dd
        ldx     dd+1
        jsr     _a3d_muldiv_
        sta     px
        stx     px+1
        ldy     yy
        dey
        clc
        lda     (po),y
        adc     px
        iny
        sta     (pt),y
        lda     (po),y                  ; (o's hi byte at y: o.x at 2, t.x at 3)
        adc     px+1
        iny
        sta     (pt),y
        rts


; the walls: x and z within +-LIM
walls:  ldy     #3
        jsr     clamp
        ldy     #5
clamp:  iny
        lda     (pt),y
        dey
        cmp     #$80
        bcs     @n
        lda     (pt),y          ; v > LIM ?
        cmp     #<(LIM + 1)
        iny
        lda     (pt),y
        sbc     #>(LIM + 1)
        dey
        bcc     @ok
        lda     #<LIM
        sta     (pt),y
        iny
        lda     #>LIM
        sta     (pt),y
        rts
@n:     lda     (pt),y          ; v < -LIM  <=>  v + LIM < 0
        clc
        adc     #<LIM
        iny
        lda     (pt),y
        adc     #>LIM
        dey
        bpl     @ok
        lda     #<(-LIM)
        sta     (pt),y
        iny
        lda     #>(-LIM)
        sta     (pt),y
@ok:    rts

.segment "BSS"
yy:     .res 1
