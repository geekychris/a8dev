; ld_phys.s - the craft's 16.16 position and velocity, in asm (port: cc65's
; 32-bit arithmetic made the C rules four times this size).
;
; ld_integrate(): for each axis, v += a * dt and p += v * dt, dt = 2/25 s:
;   a (8.8 m/s2) -> 16.16 x 0.08 = a * 20.48 = a*16 + a*4 + a/2
;   v (16.16 m/s) x 0.08 ~ (v*32 + v*8 + v) >> 9     (41/512 = 0.08008)
; then the 16-bit copies the C rules use: metres (p >> 16), the fraction's
; high byte, the velocity as 8.8.
; ld_sync(): only the copies (after C set p or v).

        .export _ld_integrate, _ld_sync, _ld_acc, _ld_p, _ld_v, _ld_m, _ld_frac, _ld_v88

.segment "LOWRAM"         ; (sdk/cfg/a8.cfg: uninitialised, $0C00-$1FFF)
_ld_acc:    .res 6          ; ax, ay, az (8.8 m/s2)
_ld_p:      .res 12         ; px, py, pz (16.16 m)
_ld_v:      .res 12         ; vx, vy, vz (16.16 m/s)
_ld_m:      .res 6          ; p >> 16 (m)
_ld_frac:   .res 3          ; (p >> 8) & 255
_ld_v88:    .res 6          ; v >> 8 (8.8 m/s)
acc:        .res 4          ; 32-bit accumulator
tmp:        .res 4
x4:         .res 1          ; axis * 4
x2:         .res 1          ; axis * 2

.segment "CODE"

; acc = sign-extended A (low) / X (high)
ld_s16: sta     acc
        stx     acc+1
        ldy     #0
        txa
        bpl     :+
        dey
:       sty     acc+2
        sty     acc+3
        rts

acc_asl:
        asl     acc
        rol     acc+1
        rol     acc+2
        rol     acc+3
        rts

acc_asr:
        lda     acc+3
        cmp     #$80
        ror     acc+3
        ror     acc+2
        ror     acc+1
        ror     acc
        rts

acc_to_tmp:
        ldy     #3
:       lda     acc,y
        sta     tmp,y
        dey
        bpl     :-
        rts

tmp_to_acc:
        ldy     #3
:       lda     tmp,y
        sta     acc,y
        dey
        bpl     :-
        rts

; tmp += acc
tmp_add:
        clc
        ldy     #0
        ldx     #4
:       lda     tmp,y
        adc     acc,y
        sta     tmp,y
        iny
        dex
        bne     :-
        rts

; void ld_integrate(void)
_ld_integrate:
        lda     #0
        sta     x4
        sta     x2
@axis:  ; ---- d = a*16 + a*4 + a/2 -> tmp
        ldy     x2
        lda     _ld_acc,y
        ldx     _ld_acc+1,y
        jsr     ld_s16
        jsr     acc_asr             ; a/2
        jsr     acc_to_tmp
        ldy     x2
        lda     _ld_acc,y
        ldx     _ld_acc+1,y
        jsr     ld_s16
        jsr     acc_asl
        jsr     acc_asl             ; a*4
        jsr     tmp_add
        jsr     acc_asl
        jsr     acc_asl             ; a*16
        jsr     tmp_add
        ; ---- v += d
        ldx     x4
        clc
        lda     _ld_v,x
        adc     tmp
        sta     _ld_v,x
        lda     _ld_v+1,x
        adc     tmp+1
        sta     _ld_v+1,x
        lda     _ld_v+2,x
        adc     tmp+2
        sta     _ld_v+2,x
        lda     _ld_v+3,x
        adc     tmp+3
        sta     _ld_v+3,x
        ; ---- e = v*32 + v*8 + v -> tmp; p += e >> 10
        lda     _ld_v,x
        sta     acc
        sta     tmp
        lda     _ld_v+1,x
        sta     acc+1
        sta     tmp+1
        lda     _ld_v+2,x
        sta     acc+2
        sta     tmp+2
        lda     _ld_v+3,x
        sta     acc+3
        sta     tmp+3
        jsr     acc_asl
        jsr     acc_asl
        jsr     acc_asl             ; v*8
        jsr     tmp_add
        jsr     acc_asl
        jsr     acc_asl             ; v*32
        jsr     tmp_add
        jsr     tmp_to_acc          ; acc = v*41
        jsr     acc_asr             ; >> 1, then >> 8 by the byte offsets below
        ldx     x4
        clc
        lda     _ld_p,x
        adc     acc+1
        sta     _ld_p,x
        lda     _ld_p+1,x
        adc     acc+2
        sta     _ld_p+1,x
        lda     acc+3               ; the sign of e >> 10 fills the top bytes
        tay
        adc     _ld_p+2,x
        sta     _ld_p+2,x
        tya
        bmi     :+
        lda     #0
        .byte   $2C                 ; bit abs: skip the lda #$FF
:       lda     #$FF
        adc     _ld_p+3,x
        sta     _ld_p+3,x
        ; next axis
        lda     x4
        clc
        adc     #4
        sta     x4
        inc     x2
        inc     x2
        cmp     #12
        beq     _ld_sync
        jmp     @axis

; the 16-bit copies of p and v
_ld_sync:
        ldx     #0
        ldy     #0
@c:     lda     _ld_p+2,x           ; metres
        sta     _ld_m,y
        lda     _ld_p+3,x
        sta     _ld_m+1,y
        lda     _ld_v+1,x           ; 8.8 velocity
        sta     _ld_v88,y
        lda     _ld_v+2,x
        sta     _ld_v88+1,y
        txa
        lsr     a
        lsr     a
        tay
        lda     _ld_p+1,x           ; the fraction
        sta     _ld_frac,y
        tya
        asl     a
        tay
        iny
        iny
        inx
        inx
        inx
        inx
        cpx     #12
        bne     @c
        rts
