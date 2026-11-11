; Uranus Lander: the ship's step, the original's 16.16 arithmetic in 6502
; (cc65 makes each 32-bit add a call; this is the hot part of a frame):
;
;   if (ship_thrust) { vx += thrust_x[angle]; vy += thrust_y[angle]; }
;   vy += gravity;  x += vx;  y += vy;
;
; Ship is the first thing in gs: x +0, y +4, vx +8, vy +12, angle +16.

        .export _ship_step, _ship_thrust
        .import _gs, _g_tune, _thrust_x, _thrust_y

SX      = _gs
SY      = _gs + 4
VX      = _gs + 8
VY      = _gs + 12
ANGLE   = _gs + 16
GRAV    = _g_tune               ; Tunables.gravity is its first field

        .bss
_ship_thrust: .res 1

        .code

; dst (4 bytes) += a 16-bit signed value at src (sign-extended)
.macro  add16s dst, src
        .local pos
        lda src+1
        and #$80
        beq pos
        lda #$FF
pos:    tay                     ; Y = the extension byte
        clc
        lda dst
        adc src
        sta dst
        lda dst+1
        adc src+1
        sta dst+1
        tya
        adc dst+2
        sta dst+2
        tya
        adc dst+3
        sta dst+3
.endmacro

.macro  add32 dst, src
        clc
        lda dst
        adc src
        sta dst
        lda dst+1
        adc src+1
        sta dst+1
        lda dst+2
        adc src+2
        sta dst+2
        lda dst+3
        adc src+3
        sta dst+3
.endmacro

        .bss
tx:     .res 2
ty:     .res 2

        .code
_ship_step:
        lda _ship_thrust
        bne :+
        jmp nothrust
:
        lda ANGLE
        asl
        tax
        lda _thrust_x,x
        sta tx
        lda _thrust_x+1,x
        sta tx+1
        lda _thrust_y,x
        sta ty
        lda _thrust_y+1,x
        sta ty+1
        lda ANGLE               ; angle 128..255: the second half of the tables
        bpl :+
        lda _thrust_x+256,x
        sta tx
        lda _thrust_x+257,x
        sta tx+1
        lda _thrust_y+256,x
        sta ty
        lda _thrust_y+257,x
        sta ty+1
:       add16s VX, tx
        add16s VY, ty
nothrust:
        add32 VY, GRAV
        add32 SX, VX
        add32 SY, VY
        rts
