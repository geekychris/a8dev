; Rock Blaster - the per-frame object loops in 6502 (cc65's C for them took
; most of a frame). What each does in C is in its comment; game.c calls them.

        .export _rocks_move, _bullets_move, _particles_move, _hit_first, _hits_build
        .export _bullet_scan, _hit_rock
        .import _bl_life
        .export _hit_px, _hit_py
        .import _rk_x, _rk_y, _rk_dx, _rk_dy, _rk_px, _rk_py, _rk_active, _rk_size
        .import _bl_x, _bl_y, _bl_dx, _bl_dy, _bl_px, _bl_py, _bl_active
        .import _pt_x, _pt_y, _pt_dx, _pt_dy, _pt_active, _pt_life, _particles_live
        .import _rock_radius_tab

W7      = 320 * 128
H7      = 216 * 128

        .bss
_hit_px: .res 2
_hit_py: .res 2
rad:    .res 1
sum:    .res 2

        .code

; pos += vel, wrapped into 0..LIMIT-1 (vel is signed; pos + vel can't pass
; 65535 since pos < 40960 and |vel| < 1024). Y = 2 * index.
.macro MOVE_WRAP pos, vel, limit
        .local neg, done, sub
        clc
        lda pos,y
        adc vel,y
        sta pos,y
        lda pos+1,y
        adc vel+1,y
        sta pos+1,y
        ldx vel+1,y
        bmi neg
        ; vel >= 0: if pos >= limit, pos -= limit
        lda pos,y
        cmp #<limit
        lda pos+1,y
        sbc #>limit
        bcc done
        lda pos,y
        sbc #<limit
        sta pos,y
        lda pos+1,y
        sbc #>limit
        sta pos+1,y
        bcs done                ; (always: no borrow)
neg:    ; vel < 0: no carry out of the add = it went below 0: pos += limit
        bcs done
        lda pos,y
        adc #<limit             ; (carry clear)
        sta pos,y
        lda pos+1,y
        adc #>limit
        sta pos+1,y
done:
.endmacro

; px = pos >> 7 (whole pixels). Y = 2 * index.
.macro WHOLE pos, px
        lda pos,y
        asl a
        lda pos+1,y
        rol a
        sta px,y
        lda #0
        rol a
        sta px+1,y
.endmacro

; for each active rock: x += dx, y += dy (wrapped), px/py = whole pixels;
; then the list of live rocks for hit_first
.proc _rocks_move
        ldx #23
loop:   lda _rk_active,x
        bne live
        jmp next
live:   stx idx
        txa
        asl a
        tay
        MOVE_WRAP _rk_x, _rk_dx, W7
        MOVE_WRAP _rk_y, _rk_dy, H7
        WHOLE _rk_x, _rk_px
        WHOLE _rk_y, _rk_py
        ldx idx
next:   dex
        bmi done
        jmp loop
done:   jmp _hits_build
.endproc

; void hits_build(void): the live rocks in slot order (as the original's
; loops meet them), with their circles: hl_*[0..hl_n-1]
.proc _hits_build
        ldy #0
        ldx #0
loop:   lda _rk_active,x
        beq next
        txa
        sta hl_id,y
        asl a
        stx idx
        tax
        lda _rk_px+1,x          ; x8 = px >> 1 (0..159), y8 = py >> 1
        lsr a
        lda _rk_px,x
        sta hl_xl,y
        ror a
        sta hl_x8,y
        lda _rk_px+1,x
        sta hl_xh,y
        lda _rk_py,x
        sta hl_yl,y
        lsr a
        sta hl_y8,y
        lda _rk_py+1,x
        sta hl_yh,y
        ldx idx
        lda _rk_size,x
        tax
        lda _rock_radius_tab,x
        sta hl_r,y
        ldx idx
        iny
next:   inx
        cpx #24
        bne loop
        sty hl_n
        rts
.endproc

.proc _bullets_move
        ldx #7
loop:   lda _bl_active,x
        bne live
        jmp next
live:
        stx idx
        txa
        asl a
        tay
        MOVE_WRAP _bl_x, _bl_dx, W7
        MOVE_WRAP _bl_y, _bl_dy, H7
        WHOLE _bl_x, _bl_px
        WHOLE _bl_y, _bl_py
        ldx idx
next:   dex
        bmi done
        jmp loop
done:   rts
.endproc

; v -= v >> 4 (arithmetic shift): port of (v * 240) / 256. v >> 4 from
; tables: low = (lo >> 4) | (hi << 4), high = hi >> 4 (sign kept)
.macro DECAY vel
        ldx vel,y
        lda shr4,x
        ldx vel+1,y
        ora shl4,x
        sta t
        sec
        lda vel,y
        sbc t
        sta vel,y
        lda vel+1,y
        sbc sar4,x
        sta vel+1,y
.endmacro

; for each active particle: x += dx, y += dy; gone if it left the world;
; dx, dy slowed; life - 1, gone at 0. particles_live counts them.
.proc _particles_move
        ldx #63
loop:   lda _pt_active,x
        bne live
        jmp next
live:   stx idx
        txa
        asl a
        tay
        clc
        lda _pt_x,y
        adc _pt_dx,y
        sta _pt_x,y
        lda _pt_x+1,y
        adc _pt_dx+1,y
        sta _pt_x+1,y
        cmp #>W7                ; >= W7 (or wrapped below 0: > $A000 too)
        bcs gone
        clc
        lda _pt_y,y
        adc _pt_dy,y
        sta _pt_y,y
        lda _pt_y+1,y
        adc _pt_dy+1,y
        sta _pt_y+1,y
        cmp #>H7                ; H7 = $6C00
        bcs gone
        DECAY _pt_dx
        DECAY _pt_dy
        ldx idx
        dec _pt_life,x
        bne next
        beq off
gone:   ldx idx
off:    lda #0
        sta _pt_active,x
        dec _particles_live
next:   dex
        bmi done
        jmp loop
done:   rts
.endproc

; unsigned char bullet_scan(unsigned char i): from bullet i on, the first
; active bullet that hits a rock (hit_first(2) at its whole pixels); its
; index, the rock in hit_rock; 0xFF if none. game.c handles the hit (score,
; split) and goes on from the next bullet, as the original's loop does.
.proc _bullet_scan
        tax
loop:   cpx #8
        bcs none
        lda _bl_active,x
        beq next
        stx bi
        txa
        asl a
        tay
        lda _bl_px,y
        sta _hit_px
        lda _bl_px+1,y
        sta _hit_px+1
        lda _bl_py,y
        sta _hit_py
        lda _bl_py+1,y
        sta _hit_py+1
        lda #2
        jsr _hit_first
        ldx bi
        cmp #$FF
        bne hit
next:   inx
        bne loop
none:   lda #$FF
        ldx #0
        rts
hit:    sta _hit_rock
        txa
        ldx #0
        rts
.endproc

; unsigned char hit_first(unsigned char r): the first live rock (slot
; order) whose circle (its radius + r) holds hit_px/hit_py:
;   dx = hit_px - px, dy = hit_py - py (whole pixels); dx*dx + dy*dy < rad*rad
; (hl_*: the list hits_build made; call it again after rocks change).
; A rock more than rad / 2 + 1 apart in half pixels (x8, y8: one byte) is
; passed over before the exact test.
.proc _hit_first
        sta rbase
        lda _hit_px+1           ; the point in half pixels
        lsr a
        lda _hit_px
        ror a
        sta px8
        lda _hit_py+1
        lsr a
        lda _hit_py
        ror a
        sta py8
        ldx #0
        cpx hl_n
        bne loop
        jmp none
loop:   lda hl_r,x
        clc
        adc rbase
        sta rad
        lsr a
        adc #2
        sta rad8
        lda px8                 ; |x8 difference| < rad / 2 + 2 ?
        sec
        sbc hl_x8,x
        bcs @x
        eor #$FF
        adc #1
@x:     cmp rad8
        bcs next
        lda py8
        sec
        sbc hl_y8,x
        bcs @y
        eor #$FF
        adc #1
@y:     cmp rad8
        bcs next
        ; |hit_px - px| < rad ?
        sec
        lda _hit_px
        sbc hl_xl,x
        sta lo
        lda _hit_px+1
        sbc hl_xh,x
        jsr absval
        bcs next
        sta absx
        sec
        lda _hit_py
        sbc hl_yl,x
        sta lo
        lda _hit_py+1
        sbc hl_yh,x
        jsr absval
        bcs next
        stx idx                 ; squares: sq(|dx|) + sq(|dy|) < sq(rad)
        tax
        ldy absx
        clc
        lda sqlo,x
        adc sqlo,y
        sta sum
        lda sqhi,x
        adc sqhi,y
        sta sum+1
        ldx rad
        lda sum
        cmp sqlo,x
        lda sum+1
        sbc sqhi,x
        ldx idx
        bcs next
        lda hl_id,x             ; hit
        ldx #0
        rts
next:   inx
        cpx hl_n
        beq none
        jmp loop
none:   lda #$FF
        ldx #0
        rts

; in: A = high byte of a 16-bit difference, lo = its low byte.
; out: C clear and A = |d| when |d| < rad, else C set (X kept)
absval: tay
        lda lo
        cpy #0
        beq @pos
        cpy #$FF
        bne @far
        eor #$FF
        clc
        adc #1
        beq @far                ; -256
@pos:   cmp rad
        bcs @far
        rts
@far:   sec
        rts
.endproc

        .bss
idx:    .res 1
bi:     .res 1
_hit_rock: .res 1
rbase:  .res 1
hl_n:   .res 1
hl_id:  .res 24
hl_xl:  .res 24
hl_xh:  .res 24
hl_yl:  .res 24
hl_yh:  .res 24
hl_r:   .res 24
hl_x8:  .res 24
hl_y8:  .res 24
px8:    .res 1
py8:    .res 1
rad8:   .res 1
absx:   .res 1
lo:     .res 1
t:      .res 2

        .rodata
shr4:   .repeat 256, i
        .byte i >> 4
        .endrepeat
shl4:   .repeat 256, i
        .byte (i << 4) & $FF
        .endrepeat
sar4:   .repeat 256, i
        .byte ((i >> 4) | ((i >> 7) * $F0)) & $FF
        .endrepeat
; squares 0..31 (rad <= 28)
sqlo:   .repeat 32, i
        .byte <(i * i)
        .endrepeat
sqhi:   .repeat 32, i
        .byte >(i * i)
        .endrepeat
