; Rock Blaster - the ship's drag and movement, and a signed multiply, in
; 6502 (cc65's 32-bit arithmetic took a quarter of a frame).

        .export _ship_move, _mul16s, _ma, _mb, _mres
        .import _ship_x, _ship_y, _ship_dx, _ship_dy, _ship_px, _ship_py

        .bss
_ma:    .res 2
_mb:    .res 2
_mres:  .res 4
a32:    .res 4
t32:    .res 4
neg:    .res 1

        .code

; v = (v * 253) / 256 with C's truncation: |v| - ceil(3|v| / 256), sign back
.macro DRAG v
        .local pos, done
        lda v+3
        sta neg
        bpl pos
        NEG32 v
pos:    ; t = 3v + 255
        lda v
        asl a
        sta t32
        lda v+1
        rol a
        sta t32+1
        lda v+2
        rol a
        sta t32+2
        lda v+3
        rol a
        sta t32+3
        clc
        lda t32
        adc v
        sta t32
        lda t32+1
        adc v+1
        sta t32+1
        lda t32+2
        adc v+2
        sta t32+2
        lda t32+3
        adc v+3
        sta t32+3
        clc
        lda t32
        adc #255
        lda t32+1
        adc #0
        sta t32+1
        lda t32+2
        adc #0
        sta t32+2
        lda t32+3
        adc #0
        sta t32+3
        ; v -= t >> 8
        sec
        lda v
        sbc t32+1
        sta v
        lda v+1
        sbc t32+2
        sta v+1
        lda v+2
        sbc t32+3
        sta v+2
        lda v+3
        sbc #0
        sta v+3
        lda neg
        bpl done
        NEG32 v
done:
.endmacro

.macro NEG32 v
        sec
        lda #0
        sbc v
        sta v
        lda #0
        sbc v+1
        sta v+1
        lda #0
        sbc v+2
        sta v+2
        lda #0
        sbc v+3
        sta v+3
.endmacro

; p += d; wrapped into 0 .. lim<<16; whole = p >> 16
.macro MOVE p, d, lim, whole
        .local low, ok
        clc
        lda p
        adc d
        sta p
        lda p+1
        adc d+1
        sta p+1
        lda p+2
        adc d+2
        sta p+2
        lda p+3
        adc d+3
        sta p+3
        bpl low
        clc                     ; < 0: += lim
        lda p+2
        adc #<lim
        sta p+2
        lda p+3
        adc #>lim
        sta p+3
        jmp ok
low:    lda p+2                 ; >= lim: -= lim
        cmp #<lim
        lda p+3
        sbc #>lim
        bcc ok
        lda p+2
        sbc #<lim
        sta p+2
        lda p+3
        sbc #>lim
        sta p+3
ok:     lda p+2
        sta whole
        lda p+3
        sta whole+1
.endmacro

; void ship_move(void): drag, move, wrap (game.c has the C)
.proc _ship_move
        DRAG _ship_dx
        DRAG _ship_dy
        MOVE _ship_x, _ship_dx, 320, _ship_px
        MOVE _ship_y, _ship_dy, 216, _ship_py
        rts
.endproc

; void mul16s(void): mres = ma * mb (signed 16 x 16 -> 32)
.proc _mul16s
        lda _ma+1
        eor _mb+1
        sta neg
        lda _ma+1               ; |ma| -> a32
        bpl @ap
        sec
        lda #0
        sbc _ma
        sta a32
        lda #0
        sbc _ma+1
        sta a32+1
        jmp @b
@ap:    lda _ma
        sta a32
        lda _ma+1
        sta a32+1
@b:     lda _mb+1               ; |mb| -> t32
        bpl @bp
        sec
        lda #0
        sbc _mb
        sta t32
        lda #0
        sbc _mb+1
        sta t32+1
        jmp @m
@bp:    lda _mb
        sta t32
        lda _mb+1
        sta t32+1
@m:     lda #0                  ; shift-add: mres = a32 * t32 (16 x 16)
        sta _mres+2
        sta _mres+3
        ldx #16
@loop:  lsr t32+1
        ror t32
        bcc @no
        clc
        lda _mres+2
        adc a32
        sta _mres+2
        lda _mres+3
        adc a32+1
        sta _mres+3
@no:    ror _mres+3
        ror _mres+2
        ror _mres+1
        ror _mres
        dex
        bne @loop
        lda neg
        bpl @done
        NEG32 _mres
@done:  rts
.endproc
