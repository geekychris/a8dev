; shell.s - a shell's flight (td_core.c's shells(), its common case in
; asm): moves shell fly_sh up to fly_n steps of (dx, dz), and stops after
; a step where something may happen: past a wall (|x| or |z| > 16000), in
; an obstacle's box (obs_next, if fly_obs) or in the box of a tank in the
; mask fly_tanks (28 units: 448 each way). The C then does that step's
; checks as before.
;
;   uint8_t shell_fly(void)   the steps made; fly_hit = 1 if it stopped early
;
; Shell: alive, owner, bounces, life, x, z, dx, dz (td.h). Tank (16 bytes,
; G.tk at G + 12): x at +3, z at +5.

        .macpack longbranch
        .export _shell_fly, _fly_sh, _fly_n, _fly_obs, _fly_tanks, _fly_hit
        .import _G, _obs_next, _oq_x, _oq_z, _oq_extra, _oq_from
        .importzp ptr1

LIM_HI  = >16000
TK      = 12
TR      = 448

.segment "BSS"
_fly_sh:    .res 2
_fly_n:     .res 1
_fly_obs:   .res 1
_fly_tanks: .res 1
_fly_hit:   .res 1
done:       .res 1
sx:         .res 2
sz:         .res 2
d:          .res 2
mask:       .res 1
tp:         .res 2

.segment "LOWXCODE"

; |v| (lo in d, hi in A) < 448 ? carry clear if so
near448:
        sta     d+1
        bpl     :+
        sec
        lda     #0
        sbc     d
        sta     d
        lda     #0
        sbc     d+1
        sta     d+1
:       lda     d
        cmp     #<TR
        lda     d+1
        sbc     #>TR
        rts

; |v| (A lo, X hi) > 16000 ? carry set if so
past:   cpx     #$80
        bcc     @pos
        ; negative: v < -16000  <=>  v + 16000 < 0
        clc
        adc     #<16000
        txa
        adc     #>16000
        bmi     @out
        clc
        rts
@pos:   ; v > 16000  <=>  16000 - v < 0
        sta     d
        stx     d+1
        sec
        lda     #<16000
        sbc     d
        lda     #>16000
        sbc     d+1
        bmi     @out
        clc
        rts
@out:   sec
        rts

_shell_fly:
        lda     #0
        sta     _fly_hit
        sta     done
        lda     _fly_sh
        sta     ptr1
        lda     _fly_sh+1
        sta     ptr1+1
@step:  lda     done
        cmp     _fly_n
        bcc     :+
        jmp     @ret
:       inc     done
        ; x += dx, z += dz (into sx, sz and the shell)
        ldy     #4
        clc
        lda     (ptr1),y
        ldy     #8
        adc     (ptr1),y
        sta     sx
        ldy     #5
        lda     (ptr1),y
        ldy     #9
        adc     (ptr1),y
        sta     sx+1
        ldy     #4
        lda     sx
        sta     (ptr1),y
        iny
        lda     sx+1
        sta     (ptr1),y
        ldy     #6
        clc
        lda     (ptr1),y
        ldy     #10
        adc     (ptr1),y
        sta     sz
        ldy     #7
        lda     (ptr1),y
        ldy     #11
        adc     (ptr1),y
        sta     sz+1
        ldy     #6
        lda     sz
        sta     (ptr1),y
        iny
        lda     sz+1
        sta     (ptr1),y
        ; the walls
        lda     sx
        ldx     sx+1
        jsr     past
        jcs     @hit
        lda     sz
        ldx     sz+1
        jsr     past
        jcs     @hit
        ; an obstacle's box
        lda     _fly_obs
        beq     @tanks
        lda     sx
        sta     _oq_x
        lda     sx+1
        sta     _oq_x+1
        lda     sz
        sta     _oq_z
        lda     sz+1
        sta     _oq_z+1
        lda     #0
        sta     _oq_extra
        sta     _oq_from
        lda     ptr1                ; (obs_next uses ptr1)
        pha
        lda     ptr1+1
        pha
        jsr     _obs_next
        tax
        pla
        sta     ptr1+1
        pla
        sta     ptr1
        cpx     #$FF
        jne     @hit
@tanks: lda     _fly_tanks
        beq     @next
        sta     mask
        lda     #<(_G + TK)
        sta     tp
        lda     #>(_G + TK)
        sta     tp+1
@tk:    lsr     mask
        bcc     @tnext
        ; |sx - t.x| < 448 and |sz - t.z| < 448 ?
        lda     tp
        sta     tq
        lda     tp+1
        sta     tq+1
        ldy     #3
        sec
        lda     sx
        sbc     (tq),y
        sta     d
        iny
        lda     sx+1
        sbc     (tq),y
        jsr     near448
        bcs     @tnext
        ldy     #5
        sec
        lda     sz
        sbc     (tq),y
        sta     d
        iny
        lda     sz+1
        sbc     (tq),y
        jsr     near448
        jcc     @hit
@tnext: lda     mask
        beq     @next
        clc
        lda     tp
        adc     #16
        sta     tp
        bcc     @tk
        inc     tp+1
        bne     @tk
@next:  jmp     @step
@hit:   lda     #1
        sta     _fly_hit
@ret:   lda     done
        ldx     #0
        rts

.segment "ZEROPAGE"
tq:     .res 2
