; Rotoball: the per-step rules that were most of a step's time in C (~1.6K
; cycles a skimmer, ~850 the CPU's choice): game.c's update_skimmer and
; cpu_controls, the same arithmetic.
;
; update_skimmer(i) with us_c (C_* bits): the original's accelerations
; ((ACCEL >> 8) x cos: forward 12, back -13; strafe 10, -11; P2 mirrored),
; the friction v * 15 / 16 truncated towards zero, the speed cap (rare: C's
; cap_check), the move and the pitch's bounds.

        .export _update_skimmer, _us_c, _cpu_controls
        .import _sk_x, _sk_z, _sk_vx, _sk_vz, _sk_ball, _ball_x, _ball_z, _cap_check
        .export _us_vx, _us_vz

C_FWD   = 1
C_BACK  = 2
C_LEFT  = 4
C_RIGHT = 8
PL_LO   = <(100 * 256)
PL_HI   = >(100 * 256)
PW_LO   = <(40 * 256)
PW_HI   = >(40 * 256)
ONE     = 256

        .segment "ZEROPAGE"
af:     .res 1                          ; signed
ar:     .res 1
vv:     .res 2
tt:     .res 2

        .bss
_us_c:  .res 1
_us_vx: .res 2                          ; (for cap_check)
_us_vz: .res 2
ix:     .res 1                          ; i * 2

; signed 16-bit compare of (lo, hi) with K: N set when less
.macro  scmp lo, hi, K
        lda lo
        cmp #<(K)
        lda hi
        sbc #>(K)
        bvc :+
        eor #$80
:
.endmacro

        .code
; v = v * 15 / 16, truncated towards zero (vv)
fric:   lda vv+1
        bpl @pos
        sec                             ; negative: -(|v| * 15 / 16)
        lda #0
        sbc vv
        sta vv
        lda #0
        sbc vv+1
        sta vv+1
        jsr @pos
        sec
        lda #0
        sbc vv
        sta vv
        lda #0
        sbc vv+1
        sta vv+1
        rts
@pos:   lda vv                          ; tt = v * 16
        sta tt
        lda vv+1
        sta tt+1
        ldx #4
:       asl tt
        rol tt+1
        dex
        bne :-
        sec                             ; - v
        lda tt
        sbc vv
        sta tt
        lda tt+1
        sbc vv+1
        ldx #4                          ; >> 4
:       lsr a
        ror tt
        dex
        bne :-
        sta vv+1
        lda tt
        sta vv
        rts

; void __fastcall__ update_skimmer(uint8_t i)
_update_skimmer:
        asl a
        sta ix
        ; the inputs: af, ar (both ways at once: 0)
        lda #0
        sta af
        sta ar
        lda _us_c
        and #C_FWD | C_BACK
        cmp #C_FWD
        bne :+
        lda #12
        sta af
:       cmp #C_BACK
        bne :+
        lda #<-13
        sta af
:       lda _us_c
        and #C_LEFT | C_RIGHT
        cmp #C_RIGHT
        bne :+
        lda #10
        sta ar
:       cmp #C_LEFT
        bne :+
        lda #<-11
        sta ar
:       ; P1: vx += af, vz -= ar; P2: vx -= af, vz += ar
        lda ix
        beq :+
        lda #0
        sec
        sbc af
        sta af
        jmp @z
:       lda #0
        sec
        sbc ar
        sta ar
@z:     ldx ix
        lda af                          ; vx += af (sign-extended)
        ldy #0
        cmp #$80
        bcc :+
        dey
:       clc
        adc _sk_vx,x
        sta vv
        tya
        adc _sk_vx+1,x
        sta vv+1
        jsr fric
        lda vv
        sta _us_vx
        lda vv+1
        sta _us_vx+1
        ldx ix
        lda ar
        ldy #0
        cmp #$80
        bcc :+
        dey
:       clc
        adc _sk_vz,x
        sta vv
        tya
        adc _sk_vz+1,x
        sta vv+1
        jsr fric
        lda vv
        sta _us_vz
        lda vv+1
        sta _us_vz+1
        ; the cap: |v| > 362 either way: C works it out (rare)
        clc
        lda _us_vx
        adc #<362
        lda _us_vx+1
        adc #>362
        cmp #>724
        bcc :+
        bne @cap
        ; (exact: (vx + 362) > 724 as unsigned)
:       clc
        lda _us_vz
        adc #<362
        sta tt
        lda _us_vz+1
        adc #>362
        cmp #>724
        bcc @move
        bne @cap
        lda tt
        cmp #<725
        bcc @move
@cap:   jsr _cap_check
@move:  ldx ix
        clc
        lda _sk_x,x
        adc _us_vx
        sta _sk_x,x
        lda _sk_x+1,x
        adc _us_vx+1
        sta _sk_x+1,x
        clc
        lda _sk_z,x
        adc _us_vz
        sta _sk_z,x
        lda _sk_z+1,x
        adc _us_vz+1
        sta _sk_z+1,x
        ; the bounds
        scmp {_sk_x,x}, {_sk_x+1,x}, 100 * 256 + 1
        bmi :+
        lda #PL_LO
        sta _sk_x,x
        lda #PL_HI
        sta _sk_x+1,x
        jsr @zvx
:       scmp {_sk_x,x}, {_sk_x+1,x}, -100 * 256
        bpl :+
        lda #<(-100 * 256)
        sta _sk_x,x
        lda #>(-100 * 256)
        sta _sk_x+1,x
        jsr @zvx
:       scmp {_sk_z,x}, {_sk_z+1,x}, 40 * 256 + 1
        bmi :+
        lda #PW_LO
        sta _sk_z,x
        lda #PW_HI
        sta _sk_z+1,x
        jsr @zvz
:       scmp {_sk_z,x}, {_sk_z+1,x}, -40 * 256
        bpl :+
        lda #<(-40 * 256)
        sta _sk_z,x
        lda #>(-40 * 256)
        sta _sk_z+1,x
        jsr @zvz
:       lda _us_vx
        sta _sk_vx,x
        lda _us_vx+1
        sta _sk_vx+1,x
        lda _us_vz
        sta _sk_vz,x
        lda _us_vz+1
        sta _sk_vz+1,x
        rts
@zvx:   lda #0
        sta _us_vx
        sta _us_vx+1
        rts
@zvz:   lda #0
        sta _us_vz
        sta _us_vz+1
        rts

; uint8_t cpu_controls(void): chase the ball, or carry it to P1's goal
; (P2 faces -X: forward is -X, right is +Z), a unit's deadband
_cpu_controls:
        ldy #0                          ; the controls
        lda _sk_ball+1
        beq @chase
        lda #<(-100 * 256)              ; the target: P1's goal (-100, 0)
        sta vv
        lda #>(-100 * 256)
        sta vv+1
        lda #0
        sta tt
        sta tt+1
        jmp @go
@chase: lda _ball_x
        sta vv
        lda _ball_x+1
        sta vv+1
        lda _ball_z
        sta tt
        lda _ball_z+1
        sta tt+1
@go:    ; tx + ONE < mx: forward
        clc
        lda vv
        adc #<ONE
        sta af
        lda vv+1
        adc #>ONE
        sta ar
        lda af
        cmp _sk_x+2
        lda ar
        sbc _sk_x+3
        bvc :+
        eor #$80
:       bpl :+
        tya
        ora #C_FWD
        tay
:       ; tx - ONE > mx: back (mx < tx - ONE)
        sec
        lda vv
        sbc #<ONE
        sta af
        lda vv+1
        sbc #>ONE
        sta ar
        lda _sk_x+2
        cmp af
        lda _sk_x+3
        sbc ar
        bvc :+
        eor #$80
:       bpl :+
        tya
        ora #C_BACK
        tay
:       ; tz - ONE > mz: right (mz < tz - ONE)
        sec
        lda tt
        sbc #<ONE
        sta af
        lda tt+1
        sbc #>ONE
        sta ar
        lda _sk_z+2
        cmp af
        lda _sk_z+3
        sbc ar
        bvc :+
        eor #$80
:       bpl :+
        tya
        ora #C_RIGHT
        tay
:       ; tz + ONE < mz: left
        clc
        lda tt
        adc #<ONE
        sta af
        lda tt+1
        adc #>ONE
        sta ar
        lda af
        cmp _sk_z+2
        lda ar
        sbc _sk_z+3
        bvc :+
        eor #$80
:       bpl :+
        tya
        ora #C_LEFT
        tay
:       tya
        ldx #0
        rts
