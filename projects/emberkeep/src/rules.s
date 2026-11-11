; Emberkeep: the enemies' patrols and the touch test (game.cpp's
; update_enemies and touching_enemy; in C they were ~1.5K cycles an enemy).

        .export _update_enemies, _touching_enemy
        .import _e_x, _e_y, _e_min, _e_max, _e_dx, _e_dy, _e_axis, _e_alive, _e_frame, _gs

GS_PX   = 13                            ; GameState: px, py
GS_NUM  = 25                            ; num_enemies

        .segment "ZEROPAGE"
pos:    .res 2
tmp:    .res 2

        .code
; each living enemy: along its axis by its speed, turning at min / max
_update_enemies:
        ldx #0
@e:     cpx _gs+GS_NUM
        bcc :+
        rts
:       lda _e_alive,x
        beq @next
        txa
        asl a
        tay                             ; Y: the int arrays
        lda _e_axis,x
        bne @vert
        lda _e_dx,x                     ; x += dx
        jsr move_x
        jmp @fr
@vert:  lda _e_dy,x
        jsr move_y
@fr:    inc _e_frame,x
@next:  inx
        jmp @e

; pos = arr[Y] + A (signed); clamp to min / max, reversing the speed
.macro  mover arr, flip
        pha
        clc
        adc arr,y
        sta pos
        pla
        and #$80
        beq :+
        lda #$FF
:       adc arr+1,y
        sta pos+1
        ; pos <= min: pos = min, speed = -speed
        lda _e_min,y
        cmp pos
        lda _e_min+1,y
        sbc pos+1
        bvc :+
        eor #$80
:       bmi :+                          ; min < pos
        lda _e_min,y
        sta pos
        lda _e_min+1,y
        sta pos+1
        jsr flip
:       ; pos >= max: pos = max, speed = -speed
        lda pos
        cmp _e_max,y
        lda pos+1
        sbc _e_max+1,y
        bvc :+
        eor #$80
:       bmi :+
        lda _e_max,y
        sta pos
        lda _e_max+1,y
        sta pos+1
        jsr flip
:       lda pos
        sta arr,y
        lda pos+1
        sta arr+1,y
        rts
.endmacro

move_x: mover _e_x, flip_dx
move_y: mover _e_y, flip_dy

flip_dx: lda #0
        sec
        sbc _e_dx,x
        sta _e_dx,x
        rts
flip_dy: lda #0
        sec
        sbc _e_dy,x
        sta _e_dy,x
        rts

; uint8_t touching_enemy(void): a living enemy's box (x + 2 .. x + 13)
; overlaps the player's: |ex - px| <= 11 and |ey - py| <= 11
_touching_enemy:
        ldx #0
@e:     cpx _gs+GS_NUM
        bcc :+
        lda #0
        tax
        rts
:       lda _e_alive,x
        beq @next
        txa
        asl a
        tay
        clc                             ; ex - px + 11 in 0..22
        lda _e_x,y
        sbc _gs+GS_PX                   ; (C clear: - 1 more)
        sta tmp
        lda _e_x+1,y
        sbc _gs+GS_PX+1
        sta tmp+1
        clc
        lda tmp
        adc #12
        sta tmp
        lda tmp+1
        adc #0
        bne @next
        lda tmp
        cmp #23
        bcs @next
        clc
        lda _e_y,y
        sbc _gs+GS_PX+2
        sta tmp
        lda _e_y+1,y
        sbc _gs+GS_PX+3
        sta tmp+1
        clc
        lda tmp
        adc #12
        sta tmp
        lda tmp+1
        adc #0
        bne @next
        lda tmp
        cmp #23
        bcs @next
        lda #1
        ldx #0
        rts
@next:  inx
        jmp @e
