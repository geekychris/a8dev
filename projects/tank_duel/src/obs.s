; obs.s - which obstacles are near a point (td_core.c's loops in C spent
; most of a tick rejecting obstacles): the Spectrum port's td_obs.asm idea.
;
;   uint8_t obs_next(void)   the first obstacle k >= oq_from that stands
;                            (hp) and whose box holds (oq_x, oq_z) widened
;                            by oq_extra units:
;                              |x - ox| < (r + extra) * 16, the same for z
;                            or $FF
; G.ob[k]: kind, hp, x (2), z (2), r: 7 bytes (td.h's Obstacle).

        .export _obs_next, _oq_x, _oq_z, _oq_extra, _oq_from
        .import _G
        .importzp ptr1


.segment "BSS"
_oq_x:     .res 2
_oq_z:     .res 2
_oq_extra: .res 1
_oq_from:  .res 1
reach:     .res 2
d:         .res 2
k:         .res 1

.segment "CODE"
        .export _obs_base, _obs_n
.segment "BSS"
_obs_base: .res 2               ; &G.ob[0] (td_core.c sets it)
_obs_n:    .res 1               ; G.nobs
.segment "CODE"

_obs_next:
        lda     _oq_from
        sta     k
        ; ptr1 = obs_base + from * 7
        asl
        asl
        asl
        sec
        sbc     k               ; from * 7 (from < 32)
        clc
        adc     _obs_base
        sta     ptr1
        lda     _obs_base+1
        adc     #0
        sta     ptr1+1
@loop:  lda     k
        cmp     _obs_n
        bcs     @none
        ldy     #1              ; hp
        lda     (ptr1),y
        beq     @next
        ; reach = (r + extra) * 16
        ldy     #6
        lda     (ptr1),y
        clc
        adc     _oq_extra
        sta     reach
        lda     #0
        rol
        asl     reach
        rol
        asl     reach
        rol
        asl     reach
        rol
        asl     reach
        rol
        sta     reach+1
        ; d = x - ox, |d| < reach ?
        ldy     #2
        sec
        lda     _oq_x
        sbc     (ptr1),y
        sta     d
        iny
        lda     _oq_x+1
        sbc     (ptr1),y
        jsr     inreach
        bcs     @next
        ldy     #4
        sec
        lda     _oq_z
        sbc     (ptr1),y
        sta     d
        iny
        lda     _oq_z+1
        sbc     (ptr1),y
        jsr     inreach
        bcs     @next
        lda     k               ; found
        ldx     #0
        rts
@next:  inc     k
        clc
        lda     ptr1
        adc     #7
        sta     ptr1
        bcc     @loop
        inc     ptr1+1
        bne     @loop
@none:  lda     #$FF
        ldx     #0
        rts

; d (lo in d, hi in A): carry clear if |d| < reach
inreach:
        sta     d+1
        bpl     @pos
        sec                     ; |d| = -d
        lda     #0
        sbc     d
        sta     d
        lda     #0
        sbc     d+1
        sta     d+1
@pos:   lda     d               ; |d| - reach: borrow (carry clear) if below
        cmp     reach
        lda     d+1
        sbc     reach+1
        rts                     ; carry set: |d| >= reach
