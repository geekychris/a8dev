; obs.s - which obstacles are near a point (td_core.c's loops in C spent
; most of a tick rejecting obstacles): the Spectrum port's td_obs.asm idea.
;
;   uint8_t obs_next(void)   the first obstacle k >= oq_from that stands
;                            (hp) and whose box holds (oq_x, oq_z) widened
;                            by oq_extra units:
;                              |x - ox| < (r + extra) * 16, the same for z
;                            or $FF
; G.ob[k]: kind, hp, x (2), z (2), r: 7 bytes (td.h's Obstacle).

        .export _obs_next, _oq_x, _oq_z, _oq_extra, _oq_from, _obs_grid
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
cell:      .res 1
left:      .res 1
xs:        .res 1
_obs_grid: .res 64 * 4          ; per cell: up to 4 obstacles, $FF the end

.segment "CODE"
        .export _obs_base, _obs_n
.segment "BSS"
_obs_base: .res 2               ; &G.ob[0] (td_core.c sets it)
_obs_n:    .res 1               ; G.nobs
.segment "CODE"

; port: a grid of 8 x 8 cells of 256 units over the arena (td_core.c's
; obs_grid): each cell lists, in order, the obstacles that can reach into it
; (their box + 40 units); a query looks only at the point's cell
_obs_next:
        ; the cell: ((x + 16384) >> 12) + ((z + 16384) >> 12) * 8, clamped
        lda     _oq_x+1
        clc
        adc     #$40
        bpl     :+
        lda     #0
:       cmp     #$80
        bcc     :+
        lda     #$7F
:       lsr
        lsr
        lsr
        lsr
        sta     cell
        lda     _oq_z+1
        clc
        adc     #$40
        bpl     :+
        lda     #0
:       cmp     #$80
        bcc     :+
        lda     #$7F
:       and     #$70
        lsr
        ora     cell
        sta     cell            ; 0..63
        asl                     ; * 4: the cell's list
        asl
        tax
        lda     #4
        sta     left
@loop:  lda     _obs_grid,x
        cmp     #$FF
        beq     @none           ; (the list's end)
        cmp     _oq_from
        bcc     @next
        sta     k
        stx     xs
        ; ptr1 = obs_base + k * 7
        asl
        asl
        asl
        sec
        sbc     k
        clc
        adc     _obs_base
        sta     ptr1
        lda     _obs_base+1
        adc     #0
        sta     ptr1+1
        jsr     test
        ldx     xs
        bcs     @next
        lda     k               ; found
        ldx     #0
        rts
@next:  inx
        dec     left
        bne     @loop
@none:  lda     #$FF
        ldx     #0
        rts

; obstacle at ptr1: carry clear if it stands and its box holds the point
test:   ldy     #1              ; hp
        lda     (ptr1),y
        bne     :+
        sec
        rts
:       ldy     #6              ; reach = (r + extra) * 16
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
        ldy     #2
        sec
        lda     _oq_x
        sbc     (ptr1),y
        sta     d
        iny
        lda     _oq_x+1
        sbc     (ptr1),y
        jsr     inreach
        bcs     @no
        ldy     #4
        sec
        lda     _oq_z
        sbc     (ptr1),y
        sta     d
        iny
        lda     _oq_z+1
        sbc     (ptr1),y
        jmp     inreach
@no:    rts

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
