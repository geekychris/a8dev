; drive.s - td_core.c's drive() in asm (every tank, every tick; 7,000
; cycles in C): turn towards dr_want by at most turn[type], move the speed
; towards dr_speed (+32 a tick up, -38 down: the original's 0.5 / 0.6 a
; tick, twice), move along the heading, then push_out (C).
;
;   void __fastcall__ drive_(Tank *t)     (dr_want, dr_speed set first)
;
; Tank: type, alive, hp, x, z, v, yaw (td.h).

        .export _drive_, _dr_want, _dr_speed, _push_out_fast
        .import _turn, _td_sin8_tab, _a3d_mulf_, _a3d_mb, _push_out
        .import _obs_next, _oq_x, _oq_z, _oq_extra, _oq_from
        .importzp ptr1

.segment "BSS"
_dr_want:  .res 2
_dr_speed: .res 2
tp:        .res 2
dd:        .res 2
rr:        .res 2
vv:        .res 2

.segment "LOWXCODE"
_drive_:
        sta     tp
        stx     tp+1
        sta     ptr1
        stx     ptr1+1
        ; d = want - yaw (16-bit wrap: the short way round)
        ldy     #9
        sec
        lda     _dr_want
        sbc     (ptr1),y
        sta     dd
        iny
        lda     _dr_want+1
        sbc     (ptr1),y
        sta     dd+1
        ; r = turn[type]
        ldy     #0
        lda     (ptr1),y
        asl
        tax
        lda     _turn,x
        sta     rr
        lda     _turn+1,x
        sta     rr+1
        ; clamp d to [-r, r]
        lda     dd+1
        bmi     @neg
        lda     dd              ; d > r ?
        cmp     rr
        lda     dd+1
        sbc     rr+1
        bcc     @turn
        lda     rr
        sta     dd
        lda     rr+1
        sta     dd+1
        jmp     @turn
@neg:   clc                     ; d < -r  <=>  d + r < 0
        lda     dd
        adc     rr
        lda     dd+1
        adc     rr+1
        bpl     @turn
        sec
        lda     #0
        sbc     rr
        sta     dd
        lda     #0
        sbc     rr+1
        sta     dd+1
@turn:  ldy     #9              ; yaw += d
        clc
        lda     (ptr1),y
        adc     dd
        sta     (ptr1),y
        iny
        lda     (ptr1),y
        adc     dd+1
        sta     (ptr1),y
        ; v towards speed
        ldy     #7
        lda     (ptr1),y
        sta     vv
        iny
        lda     (ptr1),y
        sta     vv+1
        ; compare v with speed (signed)
        sec
        lda     vv
        sbc     _dr_speed
        lda     vv+1
        sbc     _dr_speed+1
        bvc     :+
        eor     #$80
:       bmi     @up             ; v < speed
        ora     #0
        ; v >= speed: equal?
        lda     vv
        cmp     _dr_speed
        bne     @down
        lda     vv+1
        cmp     _dr_speed+1
        beq     @vok
@down:  sec                     ; v -= 38, not below speed
        lda     vv
        sbc     #38
        sta     vv
        lda     vv+1
        sbc     #0
        sta     vv+1
        sec
        lda     vv
        sbc     _dr_speed
        lda     vv+1
        sbc     _dr_speed+1
        bvc     :+
        eor     #$80
:       bpl     @vok
        lda     _dr_speed
        sta     vv
        lda     _dr_speed+1
        sta     vv+1
        jmp     @vok
@up:    clc                     ; v += 32, not above speed
        lda     vv
        adc     #32
        sta     vv
        lda     vv+1
        adc     #0
        sta     vv+1
        sec
        lda     _dr_speed
        sbc     vv
        lda     _dr_speed+1
        sbc     vv+1
        bvc     :+
        eor     #$80
:       bpl     @vok
        lda     _dr_speed
        sta     vv
        lda     _dr_speed+1
        sta     vv+1
@vok:   ldy     #7
        lda     vv
        sta     (ptr1),y
        iny
        lda     vv+1
        sta     (ptr1),y
        ; x += v * sin8[yaw >> 8] >> 8, z += v * cos8 >> 8
        ldy     #10
        lda     (ptr1),y        ; yaw's high byte: the table index
        asl
        tax
        lda     #0
        rol
        sta     rr+1            ; (index * 2: 0..510; + 128 for cos)
        txa
        sta     rr
        clc
        lda     #<_td_sin8_tab
        adc     rr
        sta     sp_
        lda     #>_td_sin8_tab
        adc     rr+1
        sta     sp_+1
        ldy     #0
        lda     (sp_),y
        sta     _a3d_mb
        iny
        lda     (sp_),y
        sta     _a3d_mb+1
        lda     vv
        ldx     vv+1
        jsr     _a3d_mulf_
        sta     dd
        stx     dd+1
        lda     tp
        sta     ptr1
        lda     tp+1
        sta     ptr1+1
        ldy     #3
        clc
        lda     (ptr1),y
        adc     dd
        sta     (ptr1),y
        iny
        lda     (ptr1),y
        adc     dd+1
        sta     (ptr1),y
        ldy     #128            ; cos = sin + 64 entries (128 bytes on)
        lda     (sp_),y
        sta     _a3d_mb
        iny
        lda     (sp_),y
        sta     _a3d_mb+1
        lda     vv
        ldx     vv+1
        jsr     _a3d_mulf_
        sta     dd
        stx     dd+1
        lda     tp
        sta     ptr1
        lda     tp+1
        sta     ptr1+1
        ldy     #5
        clc
        lda     (ptr1),y
        adc     dd
        sta     (ptr1),y
        iny
        lda     (ptr1),y
        adc     dd+1
        sta     (ptr1),y
        lda     tp
        ldx     tp+1
        ; fall through: push_out_fast

; void __fastcall__ push_out_fast(Tank *t): push_out() when an obstacle's
; box is near the tank (C), else only the arena's walls (here)
_push_out_fast:
        sta     tp
        stx     tp+1
        sta     ptr1
        stx     ptr1+1
        ldy     #3
        lda     (ptr1),y
        sta     _oq_x
        iny
        lda     (ptr1),y
        sta     _oq_x+1
        iny
        lda     (ptr1),y
        sta     _oq_z
        iny
        lda     (ptr1),y
        sta     _oq_z+1
        lda     #28                     ; TD_TANK_R
        sta     _oq_extra
        lda     #0
        sta     _oq_from
        jsr     _obs_next
        cmp     #$FF
        beq     @walls
        lda     tp
        ldx     tp+1
        jmp     _push_out
@walls: lda     tp
        sta     ptr1
        lda     tp+1
        sta     ptr1+1
        ldy     #3
        jsr     clamp
        ldy     #5
        ; fall through
; clamp the word at (ptr1),y to +-LIM (U(1000 - 28) = 15552)
clamp:  iny
        lda     (ptr1),y
        dey
        cmp     #$80                    ; (the sign: dey changed N)
        bcs     @n
        lda     (ptr1),y        ; v > LIM ?
        cmp     #<15553
        iny
        lda     (ptr1),y
        sbc     #>15553
        dey
        bcc     @ok
        lda     #<15552
        sta     (ptr1),y
        iny
        lda     #>15552
        sta     (ptr1),y
        rts
@n:     lda     (ptr1),y        ; v < -LIM  <=>  v + LIM < 0
        clc
        adc     #<15552
        iny
        lda     (ptr1),y
        adc     #>15552
        dey
        bpl     @ok
        lda     #<(-15552)
        sta     (ptr1),y
        iny
        lda     #>(-15552)
        sta     (ptr1),y
@ok:    rts

.segment "ZEROPAGE"
sp_:    .res 2
