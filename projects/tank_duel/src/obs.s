; obs.s - the obstacle scan the rules ask again and again (td_core.c), in
; asm: from obstacle Q_k on, the next one whose square of reach (its radius
; or half size + Q_r, in units) holds the point Q_x, Q_z (1/16 units);
; returns its index (and Q_k past it), Q_dx, Q_dz from it and Q_reach (1/16
; units); $FF when there are no more. Crates already broken are skipped.
; For the reaches in play (Q_r <= GRID_R) only the obstacles listed for
; the point's grid cell are looked at.
; In bank 0 with the rules (B0CODE).
        .import   _Q_x, _Q_z, _Q_dx, _Q_dz, _Q_reach, _Q_r, _Q_k
        .import   _nobs, _o_kind, _o_hp, _o_r, _o_x, _o_z
        .export   _near_obs, _grid_lo, _grid_hi

OB_CRATE = 2
OB_RMAX  = 50                       ; the widest obstacle (a pyramid's half size)
GRID_R   = 40                       ; reaches up to this from the grid (td_core.c's)

.segment "LOWRAM"
; the grid: 8 x 8 cells of 256 units, each a mask of the obstacles within
; GRID_R of it (td_core.c grid_build)
_grid_lo: .res 64
_grid_hi: .res 64
mlo:    .res 1
mhi:    .res 1
k2:     .res 1
sum:    .res 1
rmax:   .res 1
rhi:    .res 1
vlo_:   .res 1
vhi:    .res 1
tl:     .res 1
th:     .res 1

.segment "B0CODE"
_near_obs:
        lda     _Q_r                ; the widest reach's high byte (+1): most are
        clc                         ; told apart by dx's high byte alone
        adc     #OB_RMAX
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     rmax
        inc     rmax
        lda     _Q_r
        cmp     #GRID_R + 1
        bcc     @grid
        ldx     _Q_k                ; a wide reach (the set-up): all of them
@full:  cpx     _nobs
        bcs     @none
        jsr     check
        bcc     @found
        inx
        bne     @full
@none:  lda     #$FF
        ldx     #0
        rts
@grid:  lda     _Q_z+1              ; the cell: ((Q >> 8) + 64) >> 4, 0..7
        jsr     cell
        asl     a
        asl     a
        asl     a
        sta     k2
        lda     _Q_x+1
        jsr     cell
        ora     k2
        tay
        lda     _grid_lo,y          ; its obstacles from Q_k on
        sta     mlo
        lda     _grid_hi,y
        sta     mhi
        ldx     _Q_k
        beq     @g
:       lsr     mhi
        ror     mlo
        dex
        bne     :-
        ldx     _Q_k
@g:     lda     mlo
        ora     mhi
        beq     @none
        lsr     mhi
        ror     mlo
        bcc     @gn
        jsr     check
        bcc     @found
@gn:    inx
        bne     @g
@found: lda     sum                 ; found: Q_reach, Q_k = k + 1
        asl     a
        asl     a
        asl     a
        asl     a
        sta     _Q_reach
        lda     rhi
        sta     _Q_reach+1
        stx     k2
        inx
        stx     _Q_k
        lda     k2
        ldx     #0
        rts

; A = a position's high byte: its grid cell (0..7)
cell:   eor     #$80                ; + 128
        sec
        sbc     #64
        bcs     :+
        lda     #0
        rts
:       lsr     a
        lsr     a
        lsr     a
        lsr     a
        cmp     #8
        bcc     :+
        lda     #7
:       rts

; obstacle X against Q_x, Q_z: carry clear (with Q_dx, Q_dz, sum, rhi) if
; the point is within its reach
check:  txa
        asl     a
        tay                         ; Y = k * 2
        sec                         ; dx = Q_x - o_x[k]
        lda     _Q_x
        sbc     _o_x,y
        sta     _Q_dx
        lda     _Q_x+1
        sbc     _o_x+1,y
        sta     _Q_dx+1
        bpl     :+
        eor     #$FF
:       cmp     rmax
        bcs     @out
        lda     _o_kind,x           ; broken crates are gone
        cmp     #OB_CRATE
        bne     :+
        lda     _o_hp,x
        beq     @out
:       lda     _o_r,x              ; s = o_r + Q_r (units); reach = s * 16
        clc
        adc     _Q_r
        sta     sum
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     rhi
        lda     _Q_dx
        sta     vlo_
        lda     _Q_dx+1
        jsr     inreach
        bcs     @out
        sec                         ; dz = Q_z - o_z[k]
        lda     _Q_z
        sbc     _o_z,y
        sta     _Q_dz
        sta     vlo_
        lda     _Q_z+1
        sbc     _o_z+1,y
        sta     _Q_dz+1
        jmp     inreach
@out:   sec
        rts

; |v| < reach (v: A high, its low byte in _Q_dx / _Q_dz as just stored at
; the end of the subtraction, reach = rhi:sum << 4)? carry clear if so. Most
; are told apart by the high byte alone.
inreach:
        sta     vhi
        bpl     :+
        eor     #$FF                ; (~v: |v| - 1)
:       cmp     rhi
        bcc     @in                 ; high byte below: inside (but for |v| = reach exactly, by a hair)
        bne     @out
        ; the same high byte: the full compare
        lda     vhi
        bpl     @pos
        lda     vlo_                ; low byte 0: |v| is past the high byte
        beq     @out
        sec                         ; |v| = -v
        lda     #0
        sbc     vlo_
        sta     tl
        jmp     @lo
@pos:   lda     vlo_
        sta     tl
@lo:    lda     sum
        asl     a
        asl     a
        asl     a
        asl     a                   ; reach low byte
        sta     th
        lda     tl
        cmp     th
        rts                         ; carry clear when below
@in:    clc
        rts
@out:   sec
        rts
