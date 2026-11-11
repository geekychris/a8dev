; ScAIry Fun - the projection's arithmetic.
;
;   void mul_init(void)                     the tables (once)
;   int16_t __fastcall__ mulk(int16_t v)    (v * mk) >> 3, saturated (mk: 8 bits)
;   int16_t __fastcall__ mult(int16_t v)    v * mk, saturated
;   int16_t __fastcall__ divn(int16_t v)    v / dn (dn: 1..255), toward 0
;
; Multiplies by quarter squares: a * b = (a+b)^2/4 - (a-b)^2/4 from a table
; of x^2/4 (x 0..510); a 16 x 8 multiply is two of those, ~150 cycles
; (a shift-and-add loop took ~1,000). The projection's scale k = 90 * 256 /
; distance is held halved in 8 bits (mk = k / 2: the nearest boundary's
; 480 -> 240), so (v * k) >> 4 is (v * mk) >> 3.

        .export _mul_init, _mulk, _mult, _divn, _mk, _dn

        .bss
_mk:    .res 1
_dn:    .res 1
neg:    .res 1
p0:     .res 1                  ; the product, 24 bits
p1:     .res 1
p2:     .res 1
v0:     .res 1
v1:     .res 1
m0:     .res 1
q0:     .res 1
q1:     .res 1
x0:     .res 1
x1:     .res 1
x2:     .res 1

.segment "HIABSS"
sqlo:   .res 512                ; x^2 / 4
sqhi:   .res 512

.segment "HIA"

; the table: x^2 kept as a running sum (x^2 += 2x + 1), stored >> 2
.proc _mul_init
        lda #0
        sta x0
        sta x1
        sta x2
        sta q0                  ; x
        sta q1
loop:   lda x1                  ; x^2 >> 2 into v0 (lo), v1 (hi)
        sta v1
        lda x0
        lsr v1
        ror a
        lsr v1
        ror a
        sta v0
        lda x2
        sta m0
        lda x1
        lsr m0
        ror a
        lsr m0
        ror a
        sta v1
        lda q0
        tay
        lda q1
        bne hi2
        lda v0
        sta sqlo,y
        lda v1
        sta sqhi,y
        jmp step
hi2:    lda v0
        sta sqlo+256,y
        lda v1
        sta sqhi+256,y
step:   lda q0                  ; x^2 += 2x + 1
        asl a
        sta v0
        lda q1
        rol a
        sta v1
        sec
        lda x0
        adc v0
        sta x0
        lda x1
        adc v1
        sta x1
        lda x2
        adc #0
        sta x2
        inc q0
        beq :+
        jmp loop
:       inc q1
        lda q1
        cmp #2
        beq :+
        jmp loop
:       rts
.endproc

; A * Y (unsigned 8 x 8) -> q0 (lo), q1 (hi)
.proc mul8
        sta v0
        sty m0
        clc
        adc m0                  ; a + b
        tax
        bcs big
        lda sqlo,x
        sta q0
        lda sqhi,x
        sta q1
        jmp diff
big:    lda sqlo+256,x
        sta q0
        lda sqhi+256,x
        sta q1
diff:   lda v0
        sec
        sbc m0
        bcs pos
        eor #$FF
        adc #1
pos:    tax
        sec
        lda q0
        sbc sqlo,x
        sta q0
        lda q1
        sbc sqhi,x
        sta q1
        rts
.endproc

; |v| * mk into p0..p2, the sign in neg
.proc mulv
        ldy #0
        sty neg
        cpx #$80
        bcc pos
        dec neg
        eor #$FF
        clc
        adc #1
        pha
        txa
        eor #$FF
        adc #0
        tax
        pla
pos:    stx v1
        ldy _mk
        jsr mul8                ; lo(v) * m
        lda q0
        sta p0
        lda q1
        sta p1
        lda #0
        sta p2
        lda v1
        beq done
        ldy _mk
        jsr mul8                ; hi(v) * m, << 8
        clc
        lda p1
        adc q0
        sta p1
        lda q1
        adc #0
        sta p2
done:   rts
.endproc

; the signed, saturated 16-bit result from p0..p1 (p2 must be 0)
.proc result
        lda p2
        bne sat
        lda p1
        bmi sat
        lda neg
        beq plus
        sec
        lda #0
        sbc p0
        pha
        lda #0
        sbc p1
        tax
        pla
        rts
plus:   lda p0
        ldx p1
        rts
sat:    lda neg
        beq big
        lda #$01
        ldx #$80
        rts
big:    lda #$FF
        ldx #$7F
        rts
.endproc

.proc _mulk
        jsr mulv
        ldy #3
@s:     lsr p2
        ror p1
        ror p0
        dey
        bne @s
        jmp result
.endproc

.proc _mult
        jsr mulv
        jmp result
.endproc

.proc _divn
        ldy #0
        sty neg
        cpx #$80
        bcc pos
        dec neg
        eor #$FF
        clc
        adc #1
        pha
        txa
        eor #$FF
        adc #0
        tax
        pla
pos:    sta q0
        stx q1
        lda #0
        ldy #16
@d:     asl q0
        rol q1
        rol a
        bcs @sub
        cmp _dn
        bcc @next
@sub:   sbc _dn
        inc q0
@next:  dey
        bne @d
        lda q0
        sta p0
        lda q1
        sta p1
        lda #0
        sta p2
        jmp result
.endproc
