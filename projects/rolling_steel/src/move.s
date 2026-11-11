; move.s - phys.c's ball_move() in assembly: move the ball being stepped
; (phys.c's B_* scalars) by two 1/50 s steps of its velocity (in two
; halves when it's fast): walls in x, then in z (a cell more than STEPUP
; above the ball's foot, felt EDGE ahead, bounces it back with the
; surface's bounciness); then the deck under it: it follows the deck down
; while gravity would keep it there, else it flies, and lands (bouncing if
; it comes down faster than 2 u/s). See phys.c for the units.

        .export _ball_move
        .import _B_x, _B_z, _B_y, _B_xf, _B_zf, _B_yf, _B_vx, _B_vz, _B_vy
        .import _B_gr, _B_cls, _B_r, _B_g, _B_sx, _B_sz, _ph_event
        .import _cell_q, _ground_q, _cell_h, _cell_c, _g_h, _g_sx, _g_sz, _g_cls
        .importzp _cq_i, _cq_k, _gq_x, _gq_z

EDGE    = 96
STEPUP  = 64
SNAP    = 4
FAST    = 600
BOUNCE_MIN = 164
S_RAIL  = 3
PE_LAND = 32
PE_WALL = 64

.segment "BSS"
ybot:   .res 2
sh:     .res 1                  ; the move's shift: 3 (whole), 4 (a half)
dhi:    .res 2
dlo:    .res 1
np:     .res 2
nf:     .res 1
prb:    .res 2
have:   .res 1
lim:    .res 2
vv:     .res 2
bv:     .res 2
bc:     .res 1
g2:     .res 1

.segment "CODE"

_ball_move:
        lda     _B_y
        sec
        sbc     _B_r
        sta     ybot
        lda     _B_y+1
        sbc     #0
        sta     ybot+1
        lda     _B_g
        asl
        sta     g2
        lda     _B_vx
        ldx     _B_vx+1
        jsr     isfast
        bcs     @fast
        lda     _B_vz
        ldx     _B_vz+1
        jsr     isfast
        bcs     @fast
        lda     #3
        sta     sh
        jsr     movex
        jsr     movez
        jmp     vert
@fast:  lda     #4
        sta     sh
        jsr     movex
        jsr     movez
        jsr     movex
        jsr     movez
        jmp     vert

; carry set if |A/X| > FAST
isfast: cpx     #$80
        bcc     @p
        eor     #$FF            ; -v - 1 (close enough)
        pha
        txa
        eor     #$FF
        tax
        pla
@p:     cmp     #<(FAST + 1)
        txa
        sbc     #>(FAST + 1)
        rts                     ; carry: |v| >= FAST + 1

; dhi:dlo = A/X >> sh (arithmetic; the bits shifted out into dlo's top)
split:  sta     dhi
        stx     dhi+1
        lda     #0
        sta     dlo
        ldx     sh
@s:     lda     dhi+1
        cmp     #$80
        ror     dhi+1
        ror     dhi
        ror     dlo
        dex
        bne     @s
        rts

; is (cq_i, cq_k) a wall to the ball? carry set if so
wallq:  jsr     _cell_q
        cmp     #0
        beq     @no
        ; cell_h > ybot + STEPUP (signed)?
        lda     ybot
        clc
        adc     #STEPUP
        sta     lim
        lda     ybot+1
        adc     #0
        sta     lim+1
        lda     lim             ; lim - cell_h < 0 ?
        cmp     _cell_h
        lda     lim+1
        sbc     _cell_h+1
        bvc     @v
        eor     #$80
@v:     bmi     @yes
@no:    clc
        rts
@yes:   sec
        rts

; prb = np +- EDGE (by the sign of A: the velocity's high byte)
probe:  bmi     @neg
        lda     np
        clc
        adc     #EDGE
        sta     prb
        lda     np+1
        adc     #0
        sta     prb+1
        rts
@neg:   lda     np
        sec
        sbc     #EDGE
        sta     prb
        lda     np+1
        sbc     #0
        sta     prb+1
        rts

movex:  lda     _B_vx
        ldx     _B_vx+1
        jsr     split
        lda     _B_xf
        clc
        adc     dlo
        sta     nf
        lda     _B_x
        adc     dhi
        sta     np
        lda     _B_x+1
        adc     dhi+1
        sta     np+1
        lda     _B_vx+1         ; ahead: by the velocity's sign (0 counts as back)
        bmi     @b
        ora     _B_vx
        beq     @b
        lda     #0
        beq     @g
@b:     lda     #$FF
@g:     jsr     probe
        lda     prb             ; cq_i = prb >> 7
        asl
        lda     prb+1
        rol
        sta     _cq_i
        lda     #0
        rol
        sta     _cq_i+1
        lda     _B_z
        asl
        lda     _B_z+1
        rol
        sta     _cq_k
        lda     #0
        rol
        sta     _cq_k+1
        jsr     wallq
        bcc     @move
        lda     _B_vx
        ldx     _B_vx+1
        ldy     _cell_c
        jsr     bounce
        sta     _B_vx
        stx     _B_vx+1
        lda     _ph_event
        ora     #PE_WALL
        sta     _ph_event
        rts
@move:  lda     np
        sta     _B_x
        lda     np+1
        sta     _B_x+1
        lda     nf
        sta     _B_xf
        rts

movez:  lda     _B_vz
        ldx     _B_vz+1
        jsr     split
        lda     _B_zf
        clc
        adc     dlo
        sta     nf
        lda     _B_z
        adc     dhi
        sta     np
        lda     _B_z+1
        adc     dhi+1
        sta     np+1
        lda     _B_vz+1         ; ahead: by the velocity's sign (0 counts as back)
        bmi     @b
        ora     _B_vz
        beq     @b
        lda     #0
        beq     @g
@b:     lda     #$FF
@g:     jsr     probe
        lda     prb             ; cq_k = prb >> 7
        asl
        lda     prb+1
        rol
        sta     _cq_k
        lda     #0
        rol
        sta     _cq_k+1
        lda     _B_x
        asl
        lda     _B_x+1
        rol
        sta     _cq_i
        lda     #0
        rol
        sta     _cq_i+1
        jsr     wallq
        bcc     @move
        lda     _B_vz
        ldx     _B_vz+1
        ldy     _cell_c
        jsr     bounce
        sta     _B_vz
        stx     _B_vz+1
        lda     _ph_event
        ora     #PE_WALL
        sta     _ph_event
        rts
@move:  lda     np
        sta     _B_z
        lda     np+1
        sta     _B_z+1
        lda     nf
        sta     _B_zf
        rts

; bounce(v = A/X, surface Y) -> A/X: 0 below 2 u/s; else -v times .35
; (rails) or .12 (the rest)
bounce: sta     bv
        stx     bv+1
        sty     bc
        cpx     #$80
        bcs     @neg
        ; v >= 0: v < BOUNCE_MIN -> 0; else bv = -v
        cmp     #<BOUNCE_MIN
        txa
        sbc     #>BOUNCE_MIN
        bcc     @zero
        lda     #0
        sec
        sbc     bv
        sta     bv
        lda     #0
        sbc     bv+1
        sta     bv+1
        jmp     @scale
@neg:   ; v < 0: v > -BOUNCE_MIN -> 0; else bv = -v
        lda     bv
        cmp     #<(-BOUNCE_MIN)
        lda     bv+1
        sbc     #>(-BOUNCE_MIN)
        bcs     @zero           ; v >= -BOUNCE_MIN (unsigned compare of two negatives)
        lda     #0
        sec
        sbc     bv
        sta     bv
        lda     #0
        sbc     bv+1
        sta     bv+1
@scale: ; arithmetic shifts of bv: >> 2 (+ >> 4) for rails, >> 3 otherwise
        lda     bc
        cmp     #S_RAIL
        beq     @rail
        ldx     #3
        jsr     asrbv
        lda     bv
        ldx     bv+1
        rts
@rail:  ldx     #2
        jsr     asrbv
        lda     bv              ; v >> 2, and v >> 4 = that >> 2
        sta     dhi
        lda     bv+1
        sta     dhi+1
        ldx     #2
        jsr     asrbv
        lda     bv
        clc
        adc     dhi
        pha
        lda     bv+1
        adc     dhi+1
        tax
        pla
        rts
@zero:  lda     #0
        tax
        rts

asrbv:  lda     bv+1
        cmp     #$80
        ror     bv+1
        ror     bv
        dex
        bne     asrbv
        rts

; ---- up and down
vert:   lda     _B_x
        sta     _gq_x
        lda     _B_x+1
        sta     _gq_x+1
        lda     _B_z
        sta     _gq_z
        lda     _B_z+1
        sta     _gq_z+1
        jsr     _ground_q
        sta     have
        cmp     #0
        beq     @nohave
        lda     _g_sx
        sta     _B_sx
        lda     _g_sx+1
        sta     _B_sx+1
        lda     _g_sz
        sta     _B_sz
        lda     _g_sz+1
        sta     _B_sz+1
        lda     _g_cls
        sta     _B_cls
@nohave:
        lda     _B_gr
        bne     @gr
        jmp     air
@gr:    lda     have
        bne     @hv
        jmp     leave
@hv:
        ; lim = ybot + ((vy - 2g) >> 3) - SNAP
        lda     _B_vy
        sec
        sbc     g2
        sta     bv
        lda     _B_vy+1
        sbc     #0
        sta     bv+1
        ldx     #3
        jsr     asrbv
        lda     bv
        clc
        adc     ybot
        sta     lim
        lda     bv+1
        adc     ybot+1
        sta     lim+1
        lda     lim
        sec
        sbc     #SNAP
        sta     lim
        bcs     @l1
        dec     lim+1
@l1:    ; g_h >= lim (signed)?
        lda     _g_h
        cmp     lim
        lda     _g_h+1
        sbc     lim+1
        bvc     @v1
        eor     #$80
@v1:    bmi     leave
        ; g_h <= ybot + STEPUP?
        lda     ybot
        clc
        adc     #STEPUP
        sta     lim
        lda     ybot+1
        adc     #0
        sta     lim+1
        lda     lim             ; lim - g_h >= 0
        cmp     _g_h
        lda     lim+1
        sbc     _g_h+1
        bvc     @v2
        eor     #$80
@v2:    bmi     leave
        ; following the deck: vy = (g_h - ybot) << 3, y = g_h + r
        lda     _g_h
        sec
        sbc     ybot
        sta     bv
        lda     _g_h+1
        sbc     ybot+1
        asl     bv
        rol
        asl     bv
        rol
        asl     bv
        rol
        sta     _B_vy+1
        lda     bv
        sta     _B_vy
        jsr     sety
        rts
leave:  lda     #0
        sta     _B_gr
air:    ; vy -= 2g; y += vy / 8 (with the fraction)
        lda     _B_vy
        sec
        sbc     g2
        sta     _B_vy
        sta     vv
        lda     _B_vy+1
        sbc     #0
        sta     _B_vy+1
        sta     vv+1
        lda     #3
        sta     sh
        lda     vv
        ldx     vv+1
        jsr     split
        lda     _B_yf
        clc
        adc     dlo
        sta     _B_yf
        lda     _B_y
        adc     dhi
        sta     _B_y
        lda     _B_y+1
        adc     dhi+1
        sta     _B_y+1
        lda     have
        bne     @h
        rts
@h:     ; y - r <= g_h ?  (g_h - (y - r) >= 0)
        lda     _B_y
        sec
        sbc     _B_r
        sta     lim
        lda     _B_y+1
        sbc     #0
        sta     lim+1
        lda     _g_h
        cmp     lim
        lda     _g_h+1
        sbc     lim+1
        bvc     @v3
        eor     #$80
@v3:    bmi     @out
        ; ybot >= g_h - STEPUP ?
        lda     _g_h
        sec
        sbc     #STEPUP
        sta     lim
        lda     _g_h+1
        sbc     #0
        sta     lim+1
        lda     ybot
        cmp     lim
        lda     ybot+1
        sbc     lim+1
        bvc     @v4
        eor     #$80
@v4:    bmi     @out
        ; landed
        jsr     sety
        ; v < -BOUNCE_MIN: bounce
        lda     vv
        cmp     #<(-BOUNCE_MIN)
        lda     vv+1
        sbc     #>(-BOUNCE_MIN)
        bvc     @v5
        eor     #$80
@v5:    bpl     @nob
        lda     vv
        ldx     vv+1
        ldy     _g_cls
        jsr     bounce
        sta     _B_vy
        stx     _B_vy+1
        lda     _ph_event
        ora     #PE_LAND
        sta     _ph_event
@nob:   ; vy < BOUNCE_MIN: on the deck
        lda     _B_vy
        cmp     #<BOUNCE_MIN
        lda     _B_vy+1
        sbc     #>BOUNCE_MIN
        bvc     @v6
        eor     #$80
@v6:    bpl     @out
        lda     #0
        sta     _B_vy
        sta     _B_vy+1
        lda     #1
        sta     _B_gr
@out:   rts

; y = g_h + r, its fraction 0
sety:   lda     _g_h
        clc
        adc     _B_r
        sta     _B_y
        lda     _g_h+1
        adc     #0
        sta     _B_y+1
        lda     #0
        sta     _B_yf
        rts
