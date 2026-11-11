; think.s - td_ai.c's think() in asm: an enemy's mind for a tick.
;   d = |player - t|, to = the heading to the player, want = to,
;   speed = vmax[type]; the timers (ai_a 0..59, ai_b 0..199), the reload;
;   scout:   ai_a > 35: snipe (want to, speed 32, fire when lined up
;            within 700 and nearer than 900 units); else circle: to +
;            orbit * $1000 beyond 420, $6000 within 260, $4000 between;
;            ai_b = 199: the other way round
;   mortar:  back off within 450, close in beyond 750, else stand; lob
;            when loaded and within 1100
;   bulwark: stand within 240; fire when lined up within 600 and 900 units
;   moving and something 70 units ahead (td_blocked): want = yaw +
;   orbit * $2000. Then want, speed into the tank, and drive (drive.s).
; td_fire and lob are C (rare).
;
;   void __fastcall__ think(uint8_t k)
;
; Tank (20 bytes, G.tk at G + 12): type, alive, hp, x +3, z +5, v +7,
; yaw +9, reload +11, orbit +12, ai_a +13, ai_b +14, held, want +16,
; speed +18. Obstacle (7 bytes): kind, hp, x, z, r.

        .macpack longbranch
        .export _think, _th_d, _th_to
        .import _G, _vmax, _td_len_, _ln_x, _ln_z, _td_atan2_, _at_x, _at_z
        .import _td_fire, _lob, _drive_, _dr_want, _dr_speed
        .import _td_sin8_tab, _a3d_mulf_, _a3d_mb
        .import _obs_next, _oq_x, _oq_z, _oq_extra, _oq_from, _obs_base

TK      = 12
TANK_R  = 28
LIM     = 16 * (1000 - TANK_R)
CRATE   = 2

.segment "ZEROPAGE"
th:     .res 2                          ; the tank
tob:    .res 2                          ; an obstacle

.segment "BSS"
kk:     .res 1
dx:     .res 2
dz:     .res 2
_th_d:  .res 2                          ; (the last think's d and to: tests)
_th_to: .res 2
want:   .res 2
speed:  .res 2
ad:     .res 2                          ; |to - yaw|
bx:     .res 2
bz:     .res 2
reach:  .res 2

.segment "HI2CODE"

_think: sta     kk
        ; th = G.tk + k * 20
        asl
        asl
        sta     th                      ; k * 4
        asl
        asl                             ; k * 16
        clc
        adc     th
        adc     #<(_G + TK)
        sta     th
        lda     #>(_G + TK)
        adc     #0
        sta     th+1
        ; dx, dz: to the player
        ldy     #3
        sec
        lda     _G + TK + 3
        sbc     (th),y
        sta     dx
        sta     _ln_x
        iny
        lda     _G + TK + 4
        sbc     (th),y
        sta     dx+1
        sta     _ln_x+1
        iny
        sec
        lda     _G + TK + 5
        sbc     (th),y
        sta     dz
        sta     _ln_z
        iny
        lda     _G + TK + 6
        sbc     (th),y
        sta     dz+1
        sta     _ln_z+1
        jsr     _td_len_
        sta     _th_d
        stx     _th_d+1
        lda     dx
        sta     _at_x
        lda     dx+1
        sta     _at_x+1
        lda     dz
        sta     _at_z
        lda     dz+1
        sta     _at_z+1
        jsr     _td_atan2_
        sta     _th_to
        stx     _th_to+1
        sta     want
        stx     want+1
        ; speed = vmax[type]
        ldy     #0
        lda     (th),y
        asl
        tax
        lda     _vmax,x
        sta     speed
        lda     _vmax+1,x
        sta     speed+1
        ; the timers
        ldy     #13
        lda     (th),y
        clc
        adc     #1
        cmp     #60
        bcc     :+
        lda     #0
:       sta     (th),y
        iny
        lda     (th),y
        clc
        adc     #1
        cmp     #200
        bcc     :+
        lda     #0
:       sta     (th),y
        ; reload > 0: down one
        ldy     #11
        lda     (th),y
        beq     :+
        bmi     :+
        sec
        sbc     #1
        sta     (th),y
:       ; ad = |to - yaw| (16 bits: $8000 stays, "negative": below anything)
        ldy     #9
        sec
        lda     _th_to
        sbc     (th),y
        sta     ad
        iny
        lda     _th_to+1
        sbc     (th),y
        sta     ad+1
        bpl     :+
        sec
        lda     #0
        sbc     ad
        sta     ad
        lda     #0
        sbc     ad+1
        sta     ad+1
:       ldy     #0
        lda     (th),y
        cmp     #1
        beq     scout
        cmp     #2
        jeq     mortar
        cmp     #3
        jeq     bulwark
        jmp     ahead

; ---- a scout
scout:  ldy     #13
        lda     (th),y
        cmp     #36
        bcc     @circ
        lda     #32                     ; snipe
        sta     speed
        lda     #0
        sta     speed+1
        lda     #<700
        ldx     #>700
        jsr     lined
        bcs     @turn
        jsr     d900
        bcs     @turn
        lda     kk
        jsr     _td_fire
        jmp     @turn
@circ:  lda     #<6721                  ; d > 6720
        cmp     _th_d
        lda     #>6721
        sbc     _th_d+1
        bcs     :+
        lda     #$10
        bne     @orb
:       lda     _th_d                   ; d < 4160
        cmp     #<4160
        lda     _th_d+1
        sbc     #>4160
        bcs     :+
        lda     #$60
        bne     @orb
:       lda     #$40
@orb:   jsr     orbit
@turn:  ldy     #14                     ; ai_b = 199: the other way round
        lda     (th),y
        cmp     #199
        bne     :+
        ldy     #12
        lda     #0
        sec
        sbc     (th),y
        sta     (th),y
:       jmp     ahead

; want = to + orbit * (A << 8)  (orbit +-1)
orbit:  tax
        ldy     #12
        lda     (th),y
        bmi     :+
        txa
        clc
        adc     want+1
        sta     want+1
        rts
:       stx     ad                      ; (ad no longer needed)
        lda     want+1
        sec
        sbc     ad
        sta     want+1
        rts

; carry clear if ad < A/X (signed: $8000 counts as below)
lined:  sta     reach
        stx     reach+1
        lda     ad+1
        bmi     @yes
        lda     ad
        cmp     reach
        lda     ad+1
        sbc     reach+1
        rts
@yes:   clc
        rts

; carry clear if d < 14400 (900 units)
d900:   lda     _th_d
        cmp     #<14400
        lda     _th_d+1
        sbc     #>14400
        rts

; ---- a mortar
mortar: lda     _th_d                   ; d < 7200: away
        cmp     #<7200
        lda     _th_d+1
        sbc     #>7200
        bcs     :+
        lda     want+1
        eor     #$80
        sta     want+1
        jmp     @lob
:       lda     #<12001                 ; d > 12000: closer (want = to)
        cmp     _th_d
        lda     #>12001
        sbc     _th_d+1
        bcc     @lob
        lda     #0
        sta     speed
        sta     speed+1
@lob:   ldy     #11
        lda     (th),y
        bne     ahead
        lda     _th_d                   ; d < 17600
        cmp     #<17600
        lda     _th_d+1
        sbc     #>17600
        bcs     ahead
        lda     kk
        jsr     _lob
        jmp     ahead

; ---- a bulwark
bulwark:
        lda     _th_d                   ; d < 3840: stand
        cmp     #<3840
        lda     _th_d+1
        sbc     #>3840
        bcs     :+
        lda     #0
        sta     speed
        sta     speed+1
:       lda     #<600
        ldx     #>600
        jsr     lined
        bcs     ahead
        jsr     d900
        bcs     ahead
        lda     kk
        jsr     _td_fire
        ; fall through

; ---- something in the way ahead (moving): steer round it
ahead:  lda     speed
        ora     speed+1
        jeq     go
        lda     speed+1
        jmi     go
        ; (bx, bz) = t + 1120 * (sin, cos)(yaw)
        ldy     #10
        lda     (th),y                  ; yaw's high byte: the table index * 2
        asl
        sta     tob
        lda     #0
        rol
        sta     tob+1
        clc
        lda     tob
        adc     #<_td_sin8_tab
        sta     tob
        lda     tob+1
        adc     #>_td_sin8_tab
        sta     tob+1
        ldy     #0
        lda     (tob),y
        sta     _a3d_mb
        iny
        lda     (tob),y
        sta     _a3d_mb+1
        lda     #<1120
        ldx     #>1120
        jsr     _a3d_mulf_
        ldy     #3
        clc
        adc     (th),y
        sta     bx
        txa
        iny
        adc     (th),y
        sta     bx+1
        ldy     #128                    ; cos: 64 entries on
        lda     (tob),y
        sta     _a3d_mb
        iny
        lda     (tob),y
        sta     _a3d_mb+1
        lda     #<1120
        ldx     #>1120
        jsr     _a3d_mulf_
        ldy     #5
        clc
        adc     (th),y
        sta     bz
        txa
        iny
        adc     (th),y
        sta     bz+1
        jsr     blocked
        bcc     go
        ldy     #9                      ; want = yaw + orbit * $2000
        lda     (th),y
        sta     want
        iny
        lda     (th),y
        sta     want+1
        lda     #$20
        jsr     orbit
go:     ldy     #16
        lda     want
        sta     (th),y
        sta     _dr_want
        iny
        lda     want+1
        sta     (th),y
        sta     _dr_want+1
        iny
        lda     speed
        sta     (th),y
        sta     _dr_speed
        iny
        lda     speed+1
        sta     (th),y
        sta     _dr_speed+1
        lda     th
        ldx     th+1
        jmp     _drive_

; carry set if (bx, bz) is past a wall (radius 28) or in an obstacle
blocked:
        lda     bx
        ldx     bx+1
        jsr     past
        jcs     @yes
        lda     bz
        ldx     bz+1
        jsr     past
        jcs     @yes
        lda     bx
        sta     _oq_x
        lda     bx+1
        sta     _oq_x+1
        lda     bz
        sta     _oq_z
        lda     bz+1
        sta     _oq_z+1
        lda     #TANK_R
        sta     _oq_extra
        lda     #0
        sta     _oq_from
@next:  jsr     _obs_next
        cmp     #$FF
        bne     :+
        clc
        rts
:       tax
        inx
        stx     _oq_from
        sta     tob                     ; tob = obs_base + k * 7
        asl
        asl
        asl
        sec
        sbc     tob
        clc
        adc     _obs_base
        sta     tob
        lda     _obs_base+1
        adc     #0
        sta     tob+1
        ldy     #0
        lda     (tob),y
        cmp     #CRATE
        beq     @yes
        ldy     #6                      ; reach = (r + 28) * 16
        lda     (tob),y
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
        ldy     #2                      ; |(bx - o.x, bz - o.z)| < reach ?
        sec
        lda     bx
        sbc     (tob),y
        sta     _ln_x
        iny
        lda     bx+1
        sbc     (tob),y
        sta     _ln_x+1
        iny
        sec
        lda     bz
        sbc     (tob),y
        sta     _ln_z
        iny
        lda     bz+1
        sbc     (tob),y
        sta     _ln_z+1
        jsr     _td_len_
        cmp     reach
        txa
        sbc     reach+1
        bcs     @next
@yes:   sec
        rts

; |A/X| > LIM ? carry set if so
past:   cpx     #$80
        bcc     @pos
        clc                             ; v < -LIM  <=>  v + LIM < 0
        adc     #<LIM
        txa
        adc     #>LIM
        bmi     @out
        clc
        rts
@pos:   sta     reach                   ; v > LIM  <=>  LIM - v < 0
        stx     reach+1
        sec
        lda     #<LIM
        sbc     reach
        lda     #>LIM
        sbc     reach+1
        bmi     @out
        clc
        rts
@out:   sec
        rts
