; physasm.s - move_ball's fast path in 6502 assembly (pb_phys.c has the
; original's C for the rest: mb_cap, mb_seg, mb_circ, mb_sens, mb_flip,
; mb_tail). It runs from bank 0 with the physics.
;
; void __fastcall__ move_ball(World *w, Ball *b, int16_t grav):
;   b->vy -= grav; the speed cap (mb_cap) when |vx| + |vy| > 7000 mm/s;
;   x += vx dt, y += vy dt (the original's advance(): P with an 8-bit
;   fraction, v * sub_kpos >> 9: port, Q9); then the ball's grid cell (64 mm): for
;   each entry, a box test (a wall's box, a post's or bumper's square of
;   its radius + the ball's, a sensor's box) and, when it passes, the C;
;   each flipper within reach of its pivot; then mb_tail (the saucer, the
;   plunger, the drain). The order is the original's.

        .export _move_ball
        .import _mb_cap, _mb_seg, _mb_seg_end, _mb_seg_hit, _mb_circ, _mb_sens, _mb_flip, _mb_tail
        .import _mb_sx0, _mb_sy0, _mb_sx1, _mb_sy1, _mb_sp
        .import _mb_cx, _mb_cy, _mb_crr, _mb_cp
        .import _mb_nx0, _mb_ny0, _mb_nx1, _mb_ny1, _mb_np
        .import _mb_fpx, _mb_fpy, _mb_freach, _mb_fp, _mb_nflip
        .import _mb_grid_at, _mb_grid, _mb_x0, _mb_y0, _sub_kpos
        .import _mb_saucer_x, _mb_saucer_y, _mb_saucer_r
        .import smul_mm, ma, mb, pr
        .import pushax, incsp4, popax
        .importzp tmp1, tmp2, tmp3, tmp4
.if .version < $0300            ; cc65 2.18 says $0320 (old encoding), 2.19 git $0213
        .importzp c_sp
.else
        .importzp sp
c_sp    = sp
.endif
        .importzp ptr4

B_XF    = 3                     ; Ball (pbz.h; cc65 packs structs)
B_YF    = 4
B_X     = 5
B_Y     = 7
B_VX    = 9
B_VY    = 11
MAXSP   = 28000                 ; PB_MAX_SPEED, V
GX      = 9                     ; PB_GX
GY      = 19

.segment "B0BSS"
mw:     .word 0
mbp:    .word 0
grav:   .word 0
bx:     .word 0
by:     .word 0
t1:     .word 0
t2:     .word 0
kk:     .word 0                 ; the grid entry's address
kend:   .word 0
idx:    .byte 0
dirty:  .byte 0                 ; a C call may have moved the ball: getpos again
fi:     .byte 0

.segment "B0CODE"

; reload the ball pointer (C calls may use ptr4) and its position
.macro  ldb
        lda     mbp
        sta     ptr4
        lda     mbp+1
        sta     ptr4+1
.endmacro

; push w and b for a C call; the third argument in A/X
.macro  push_wb
        lda     mw
        ldx     mw+1
        jsr     pushax
        lda     mbp
        ldx     mbp+1
        jsr     pushax
.endmacro

; signed 16-bit: branch to `lt` if a < b (a, b absolute words)
.macro  blt16 va, vb, lt
        .local  ok
        lda     va
        cmp     vb
        lda     va+1
        sbc     vb+1
        bvc     ok
        eor     #$80
ok:     bmi     lt
.endmacro

; the same with b indexed by Y (a word array)
.macro  blt16y va, arr, lt
        .local  ok
        lda     va
        cmp     arr,y
        lda     va+1
        sbc     arr+1,y
        bvc     ok
        eor     #$80
ok:     bmi     lt
.endmacro
.macro  bgt16y va, arr, gt      ; va > arr[y]  <=>  arr[y] < va
        .local  ok
        lda     arr,y
        cmp     va
        lda     arr+1,y
        sbc     va+1
        bvc     ok
        eor     #$80
ok:     bmi     gt
.endmacro

; x += v dt: (p << 8 | frac) += (v * sub_kpos) >> 9 (port: Q9); Y = the field's offset
; (B_X or B_Y), X = the fraction's (B_XF or B_YF), v in ma
advance:
        stx     t1
        sty     t1+1
        lda     _sub_kpos
        sta     mb
        lda     _sub_kpos+1
        sta     mb+1
        jsr     smul_mm
        ldx     #1                      ; pr >>= 1 (arithmetic): then bytes 1-3 are >> 9 (port: kpos Q9)
@sh:    lda     pr+3
        cmp     #$80
        ror     pr+3
        ror     pr+2
        ror     pr+1
        dex
        bne     @sh
        ldb
        ldy     t1                      ; the fraction
        lda     (ptr4),y
        clc
        adc     pr+1
        sta     (ptr4),y
        ldy     t1+1                    ; the position (16 bits)
        lda     (ptr4),y
        adc     pr+2
        sta     (ptr4),y
        iny
        lda     (ptr4),y
        adc     pr+3
        sta     (ptr4),y
        rts

; bx, by = the ball's position now
getpos: ldb
        ldy     #B_X
        lda     (ptr4),y
        sta     bx
        iny
        lda     (ptr4),y
        sta     bx+1
        iny
        lda     (ptr4),y
        sta     by
        iny
        lda     (ptr4),y
        sta     by+1
        rts

_move_ball:
        sta     grav
        stx     grav+1
        ldy     #3
        lda     (c_sp),y
        sta     mw+1
        dey
        lda     (c_sp),y
        sta     mw
        dey
        lda     (c_sp),y
        sta     mbp+1
        dey
        lda     (c_sp),y
        sta     mbp
        jsr     incsp4
        ldb
        ; b->vy -= grav
        ldy     #B_VY
        lda     (ptr4),y
        sec
        sbc     grav
        sta     (ptr4),y
        iny
        lda     (ptr4),y
        sbc     grav+1
        sta     (ptr4),y
        ; |vx| + |vy| > MAXSP: the cap
        ldy     #B_VX
        jsr     absw
        sta     t2
        stx     t2+1
        ldy     #B_VY
        jsr     absw
        clc
        adc     t2
        sta     t2
        txa
        adc     t2+1
        sta     t2+1
        lda     #<MAXSP
        cmp     t2
        lda     #>MAXSP
        sbc     t2+1
        bcs     mv_nocap                  ; MAXSP >= sum
        lda     mbp
        ldx     mbp+1
        jsr     _mb_cap
mv_nocap: jsr     getpos
        lda     bx
        sta     _mb_x0
        lda     bx+1
        sta     _mb_x0+1
        lda     by
        sta     _mb_y0
        lda     by+1
        sta     _mb_y0+1
        ; advance x, then y
        ldy     #B_VX
        lda     (ptr4),y
        sta     ma
        iny
        lda     (ptr4),y
        sta     ma+1
        ldx     #B_XF
        ldy     #B_X
        jsr     advance
        ldb
        ldy     #B_VY
        lda     (ptr4),y
        sta     ma
        iny
        lda     (ptr4),y
        sta     ma+1
        ldx     #B_YF
        ldy     #B_Y
        jsr     advance
        jsr     getpos
        lda     #0
        sta     dirty
        ; the cell: gx = x >> 10, gy = (y + 1024) >> 10, clamped
        lda     bx+1
        cmp     #$80
        ror
        cmp     #$80
        ror                             ; x >> 10 (arithmetic), -32..31
        bpl     mv_gx0
        lda     #0
mv_gx0:   cmp     #GX
        bcc     mv_gx1
        lda     #GX - 1
mv_gx1:   sta     t1                      ; gx
        lda     by+1
        clc
        adc     #4                      ; + 1024
        bvc     mv_gyv
        lda     #$7F                    ; (overflowed: far above)
mv_gyv:   cmp     #$80
        ror
        cmp     #$80
        ror
        bpl     mv_gy0
        lda     #0
mv_gy0:   cmp     #GY
        bcc     mv_gy1
        lda     #GY - 1
mv_gy1:   ; cell = gy * 9 + gx
        sta     t1+1
        asl
        asl
        asl
        clc
        adc     t1+1
        clc
        adc     t1                      ; < 171
        ; kk = grid_at + cell * 2: k and end
        asl
        sta     t2
        lda     #0
        rol
        sta     t2+1
        lda     _mb_grid_at
        clc
        adc     t2
        sta     ptr4
        lda     _mb_grid_at+1
        adc     t2+1
        sta     ptr4+1
        ldy     #0
        lda     (ptr4),y                ; k
        sta     t2
        iny
        lda     (ptr4),y
        sta     t2+1
        iny
        lda     (ptr4),y                ; end
        sta     kend
        iny
        lda     (ptr4),y
        sta     kend+1
        ; kk = grid + k * 2, kend = grid + end * 2
        asl     t2
        rol     t2+1
        lda     _mb_grid
        clc
        adc     t2
        sta     kk
        lda     _mb_grid+1
        adc     t2+1
        sta     kk+1
        asl     kend
        rol     kend+1
        lda     _mb_grid
        clc
        adc     kend
        sta     kend
        lda     _mb_grid+1
        adc     kend+1
        sta     kend+1
mv_entry: lda     kk
        cmp     kend
        bne     mv_more
        lda     kk+1
        cmp     kend+1
        bne     mv_more
        jmp     mv_flips
mv_more:  lda     kk
        sta     ptr4
        lda     kk+1
        sta     ptr4+1
        ldy     #0
        lda     (ptr4),y                ; the index (< 256)
        asl
        sta     idx                     ; * 2: the word arrays' offset
        iny
        lda     (ptr4),y
        lsr
        lsr
        lsr
        lsr                             ; the type
        pha
        lda     kk                      ; next
        clc
        adc     #2
        sta     kk
        bcc     mv_nk
        inc     kk+1
mv_nk:    lda     dirty
        beq     mv_fresh
        jsr     getpos
        lda     #0
        sta     dirty
mv_fresh: pla
        bne     mv_ns
        jmp     mv_seg
mv_ns:
        cmp     #1
        bne     mv_ns2
        jmp     mv_circ
mv_ns2: jmp     mv_sens

mv_next: jmp    mv_entry
mv_seg:   ldy     idx                     ; bx < x0 || bx > x1 || by < y0 || by > y1: next
        blt16y  bx, _mb_sx0, mv_next
        bgt16y  bx, _mb_sx1, mv_next
        blt16y  by, _mb_sy0, mv_next
        bgt16y  by, _mb_sy1, mv_next
        ldy     idx
.ifdef SEGCHECK
        push_wb
        ldy     idx
        lda     _mb_sp,y
        ldx     _mb_sp+1,y
        jsr     _mb_seg
        inc     dirty
        jmp     mv_entry
.else
        ; port: pb_phys.c's mb_seg without the C for a miss: a dropped
        ; target is skipped; the test counted; seg_fast; the C for an end
        ; (mb_seg_end) or a contact's speed (mb_seg_hit)
        lda     _mb_sp,y
        sta     ptr3
        lda     _mb_sp+1,y
        sta     ptr3+1
        ldy     #S_KIND
        lda     (ptr3),y
        cmp     #SK_DROP
        bne     @t
        ldy     #S_UP
        lda     (ptr3),y
        bne     @t
        jmp     mv_entry
@t:     lda     mw              ; w->tests++ (offset 535)
        clc
        adc     #<(W_TESTS - 512)
        sta     ptr1
        lda     mw+1
        adc     #>(W_TESTS - 512) + 2
        sta     ptr1+1
        ldy     #0
        lda     (ptr1),y
        clc
        adc     #1
        sta     (ptr1),y
        iny
        lda     (ptr1),y
        adc     #0
        sta     (ptr1),y
        lda     mbp
        ldx     mbp+1
        jsr     pushax
        lda     ptr3
        ldx     ptr3+1
        jsr     _seg_fast
        cpx     #$80
        bne     @r
        cmp     #0
        bne     @r
        push_wb                 ; an end: the C
        ldy     idx
        lda     _mb_sp,y
        ldx     _mb_sp+1,y
        jsr     _mb_seg_end
        inc     dirty
        jmp     mv_entry
@r:     cpx     #$80            ; -1: missed, the ball untouched
        bcc     @hit
        jmp     mv_entry
@hit:   inc     dirty           ; touching (0) or a contact: the ball was moved
        stx     tmp4
        ora     tmp4
        bne     @imp
        jmp     mv_entry
@imp:   ldx     tmp4
        sta     t1
        stx     t1+1
        push_wb
        ldy     idx
        lda     _mb_sp,y
        ldx     _mb_sp+1,y
        jsr     pushax
        lda     t1
        ldx     t1+1
        jsr     _mb_seg_hit
        jmp     mv_entry
.endif

mv_circ:  ldy     idx                     ; |bx - cx| < rr && |by - cy| < rr
        lda     bx
        sec
        sbc     _mb_cx,y
        sta     t1
        lda     bx+1
        sbc     _mb_cx+1,y
        bpl     mv_c1
        tax
        lda     #0
        sec
        sbc     t1
        sta     t1
        txa
        eor     #$FF
        adc     #0
mv_c1:    sta     t1+1
        lda     t1                      ; |dx| >= rr: next
        cmp     _mb_crr,y
        lda     t1+1
        sbc     _mb_crr+1,y
        bcc     mv_c2
        jmp     mv_entry
mv_c2:    lda     by
        sec
        sbc     _mb_cy,y
        sta     t1
        lda     by+1
        sbc     _mb_cy+1,y
        bpl     mv_c3
        tax
        lda     #0
        sec
        sbc     t1
        sta     t1
        txa
        eor     #$FF
        adc     #0
mv_c3:    sta     t1+1
        lda     t1
        cmp     _mb_crr,y
        lda     t1+1
        sbc     _mb_crr+1,y
        bcc     mv_c4
        jmp     mv_entry
mv_c4:    push_wb
        ldy     idx
        lda     _mb_cp,y
        ldx     _mb_cp+1,y
        jsr     _mb_circ
        inc     dirty
        jmp     mv_entry

mv_sens:  ldy     idx
        blt16y  bx, _mb_nx0, mv_snext
        bgt16y  bx, _mb_nx1, mv_snext
        blt16y  by, _mb_ny0, mv_snext
        bgt16y  by, _mb_ny1, mv_snext
        push_wb
        ldy     idx
        lda     _mb_np,y
        ldx     _mb_np+1,y
        jsr     _mb_sens
        inc     dirty
        cmp     #0
        beq     mv_snext
        rts                             ; onto a ramp: the original returns
mv_snext: jmp     mv_entry

        ; the flippers: |bx - px| > reach || |by - py| > reach: next
mv_flips: lda     #0
        sta     fi
mv_fl:    lda     fi
        cmp     _mb_nflip
        bcs     mv_tail
        asl
        tay
        jsr     getpos
        ldy     fi
        tya
        asl
        tay
        lda     bx
        sec
        sbc     _mb_fpx,y
        sta     t1
        lda     bx+1
        sbc     _mb_fpx+1,y
        jsr     absa
        bgt16y  t1, _mb_freach, mv_fnext
        lda     by
        sec
        sbc     _mb_fpy,y
        sta     t1
        lda     by+1
        sbc     _mb_fpy+1,y
        jsr     absa
        bgt16y  t1, _mb_freach, mv_fnext
        push_wb
        lda     fi
        asl
        tay
        lda     _mb_fp,y
        ldx     _mb_fp+1,y
        jsr     _mb_flip
mv_fnext: inc     fi
        jmp     mv_fl
mv_tail:  jsr     getpos
        ; the plunger (x > 478 mm) or the drain (y < -40 mm): the C
        lda     #<7648
        cmp     bx
        lda     #>7648
        sbc     bx+1
        bvc     mv_t1
        eor     #$80
mv_t1:  bmi     mv_c                    ; 7648 < x
        lda     by
        cmp     #<(-640)
        lda     by+1
        sbc     #>(-640)
        bvc     mv_t2
        eor     #$80
mv_t2:  bmi     mv_c                    ; y < -640
        ; the saucer's box: |x - sx| < sr && |y - sy| < sr
        lda     bx
        sec
        sbc     _mb_saucer_x
        sta     t1
        lda     bx+1
        sbc     _mb_saucer_x+1
        jsr     absa
        lda     t1
        cmp     _mb_saucer_r
        lda     t1+1
        sbc     _mb_saucer_r+1
        bcs     mv_tret
        lda     by
        sec
        sbc     _mb_saucer_y
        sta     t1
        lda     by+1
        sbc     _mb_saucer_y+1
        jsr     absa
        lda     t1
        cmp     _mb_saucer_r
        lda     t1+1
        sbc     _mb_saucer_r+1
        bcs     mv_tret
mv_c:   lda     mw
        ldx     mw+1
        jsr     pushax
        lda     mbp
        ldx     mbp+1
        jmp     _mb_tail
mv_tret: rts

; t1 = |t1|, with A = t1's high byte on entry (Y kept)
absa:   sta     t1+1
        bpl     @p
        lda     #0
        sec
        sbc     t1
        sta     t1
        lda     #0
        sbc     t1+1
        sta     t1+1
@p:     rts

; A/X = |the ball's word at offset Y|
absw:   lda     (ptr4),y
        sta     t1
        iny
        lda     (ptr4),y
        jsr     absa
        lda     t1
        ldx     t1+1
        rts

; ---------------------------------------------------------------------------
; int16_t __fastcall__ seg_fast(Ball *b, const Seg *s): hit_segment's
; interior case (pb_phys.c has the C it follows, integer for integer): when
; the ball's projection onto the segment is strictly inside it, the contact
; normal is the segment's (+/-(-uy, ux)). Returns $8000 when the projection
; is at or past an end (the C does the end caps), else what hit_segment
; returns (-1 no contact, else the approach speed).

        .export _seg_fast
        .importzp ptr3

S_AX    = 0                     ; Seg (pbz.h)
S_KIND  = 25
S_UP    = 30
SK_DROP = 5
W_TESTS = 535
S_AY    = 2
S_UX    = 8
S_UY    = 10
S_LEN   = 12
S_R     = 14
S_E     = 24
S_OWX   = 27
S_OWY   = 28
PB_R    = 216
SLOW    = 240                   ; V_MM(60)

.segment "B0BSS"
sdx:    .word 0
sdy:    .word 0
sux:    .word 0
suy:    .word 0
acc:    .res 4
tt:     .res 4
snx:    .word 0
sny:    .word 0
svn:    .word 0
srr:    .word 0
sqx:    .word 0
sqy:    .word 0
stx_:   .word 0
sty_:   .word 0
sen:    .word 0
sk:     .word 0
see:    .word 0
bq:     .word 0                 ; (umul uses tmp1-tmp4)

.segment "B0CODE"

; pr = A/X * the word at (Y): signed (ma = A/X, mb = word)
.macro  mulw  w
        sta     ma
        stx     ma+1
        lda     w
        sta     mb
        lda     w+1
        sta     mb+1
        jsr     smul_mm
.endmacro

; A/X = pr >> 14 (the product's bits 14..29)
q14:    lda     pr+2
        sta     tmp1
        lda     pr+3
        sta     tmp2
        asl     tmp1
        rol     tmp2
        asl     tmp1
        rol     tmp2
        lda     pr+1
        lsr
        lsr
        lsr
        lsr
        lsr
        lsr
        ora     tmp1
        ldx     tmp2
        rts

; acc = pr
.macro  pr2acc
        lda     pr
        sta     acc
        lda     pr+1
        sta     acc+1
        lda     pr+2
        sta     acc+2
        lda     pr+3
        sta     acc+3
.endmacro
; pr += acc
addacc: lda     pr
        clc
        adc     acc
        sta     pr
        lda     pr+1
        adc     acc+1
        sta     pr+1
        lda     pr+2
        adc     acc+2
        sta     pr+2
        lda     pr+3
        adc     acc+3
        sta     pr+3
        rts
; pr = acc - pr
subacc: lda     acc
        sec
        sbc     pr
        sta     pr
        lda     acc+1
        sbc     pr+1
        sta     pr+1
        lda     acc+2
        sbc     pr+2
        sta     pr+2
        lda     acc+3
        sbc     pr+3
        sta     pr+3
        rts

; the Seg's word at offset Y -> A/X
.macro  sw  off
        ldy     #off+1
        lda     (ptr3),y
        tax
        dey
        lda     (ptr3),y
.endmacro
; the Ball's word at offset -> A/X
.macro  bw  off
        ldy     #off+1
        lda     (ptr4),y
        tax
        dey
        lda     (ptr4),y
.endmacro

_seg_fast:
        sta     ptr3            ; s
        stx     ptr3+1
        jsr     popax           ; b
        sta     ptr4
        stx     ptr4+1
        ; dx = b.x - s.ax, dy = b.y - s.ay; ux, uy
        ldy     #B_X
        lda     (ptr4),y
        ldy     #S_AX
        sec
        sbc     (ptr3),y
        sta     sdx
        ldy     #B_X+1
        lda     (ptr4),y
        ldy     #S_AX+1
        sbc     (ptr3),y
        sta     sdx+1
        ldy     #B_Y
        lda     (ptr4),y
        ldy     #S_AY
        sec
        sbc     (ptr3),y
        sta     sdy
        ldy     #B_Y+1
        lda     (ptr4),y
        ldy     #S_AY+1
        sbc     (ptr3),y
        sta     sdy+1
        sw      S_UX
        sta     sux
        stx     sux+1
        sw      S_UY
        sta     suy
        stx     suy+1
        ; tt = (dx ux + dy uy) >> 14 (32 bits, arithmetic)
        lda     sdx
        ldx     sdx+1
        mulw    sux
        pr2acc
        lda     sdy
        ldx     sdy+1
        mulw    suy
        jsr     addacc
        ldx     #14             ; pr >>= 14
@sh:    lda     pr+3
        cmp     #$80
        ror     pr+3
        ror     pr+2
        ror     pr+1
        ror     pr
        dex
        bne     @sh
        lda     pr
        sta     tt
        lda     pr+1
        sta     tt+1
        lda     pr+2
        sta     tt+2
        lda     pr+3
        sta     tt+3
        ; 0 < tt < len (len > 0, 16 bits): tt's high word must be 0 and tt != 0
        ora     pr+2
        bne     @ends
        lda     pr
        ora     pr+1
        beq     @ends
        lda     pr+1
        bmi     @ends           ; >= 32768 > len
        ldy     #S_LEN
        lda     pr
        cmp     (ptr3),y
        iny
        lda     pr+1
        sbc     (ptr3),y
        bcs     @ends
        jmp     @inside         ; tt < len
@ends:  ; port: past an end, the C (hit_segment) takes the end's circle. Most
        ; of the time the ball is nowhere near it: if the ball's box misses
        ; the end point by rr + 2 (the C's q is the end to within 1 P of
        ; rounding), it's -1 here, the same as the C would say
        ldy     #S_R
        lda     (ptr3),y
        clc
        adc     #<(PB_R + 2)
        sta     srr
        iny
        lda     (ptr3),y
        adc     #>(PB_R + 2)
        sta     srr+1
        ldy     #S_AX           ; tt <= 0: the a end, else the b end
        lda     tt+3
        bmi     @ea
        ora     tt+2
        ora     tt+1
        ora     tt
        beq     @ea
        ldy     #S_AX + 4
@ea:    sty     tmp4
        ; |b.x - end.x| >= rr + 2 or |b.y - end.y| >= rr + 2: missed
        ldy     #B_X
        lda     (ptr4),y
        ldy     tmp4
        sec
        sbc     (ptr3),y
        sta     tmp1
        ldy     #B_X+1
        lda     (ptr4),y
        ldy     tmp4
        iny
        sbc     (ptr3),y
        jsr     @far
        bcs     @miss
        ldy     #B_Y
        lda     (ptr4),y
        ldy     tmp4
        iny
        iny
        sec
        sbc     (ptr3),y
        sta     tmp1
        ldy     #B_Y+1
        lda     (ptr4),y
        ldy     tmp4
        iny
        iny
        iny
        sbc     (ptr3),y
        jsr     @far
        bcs     @miss
        lda     #$00            ; near the end: the C
        ldx     #$80
        rts
@miss:  lda     #$FF
        tax
        rts
; carry set if |A:tmp1| >= srr
@far:   tax
        bpl     @fp
        lda     #0
        sec
        sbc     tmp1
        sta     tmp1
        txa
        eor     #$FF
        adc     #0
        tax
@fp:    lda     tmp1
        cmp     srr
        txa
        sbc     srr+1
        rts
@inside:
        ; dist = (dy ux - dx uy) >> 14
        lda     sdy
        ldx     sdy+1
        mulw    sux
        pr2acc
        lda     sdx
        ldx     sdx+1
        mulw    suy
        jsr     subacc
        jsr     q14             ; dist in A/X (the low 16 bits of the >> 14)
        sta     tmp3
        stx     tmp4
        ; rr = PB_R + s.r
        ldy     #S_R
        lda     (ptr3),y
        clc
        adc     #<PB_R
        sta     srr
        iny
        lda     (ptr3),y
        adc     #>PB_R
        sta     srr+1
        ; n = dist >= 0 ? (-uy, ux) : (uy, -ux); ad = |dist|
        lda     tmp4
        bmi     @neg
        lda     #0
        sec
        sbc     suy
        sta     snx
        lda     #0
        sbc     suy+1
        sta     snx+1
        lda     sux
        sta     sny
        lda     sux+1
        sta     sny+1
        jmp     @adist
@neg:   lda     suy
        sta     snx
        lda     suy+1
        sta     snx+1
        lda     #0
        sec
        sbc     sux
        sta     sny
        lda     #0
        sbc     sux+1
        sta     sny+1
        lda     #0              ; ad = -dist
        sec
        sbc     tmp3
        sta     tmp3
        lda     #0
        sbc     tmp4
        sta     tmp4
@adist: ; ad >= rr: no contact (unsigned compare: both >= 0)
        lda     tmp3
        cmp     srr
        lda     tmp4
        sbc     srr+1
        bcc     @touch
        lda     #$FF
        tax
        rts
@touch: ; vn = (vx nx + vy ny) >> 14
        bw      B_VX
        mulw    snx
        pr2acc
        bw      B_VY
        mulw    sny
        jsr     addacc
        jsr     q14
        sta     svn
        stx     svn+1
        ; a one-way gate: (nx ow_x + ny ow_y <= 0 || vn >= 0): no contact
        ldy     #S_OWX
        lda     (ptr3),y
        iny
        ora     (ptr3),y
        beq     @twoway
        lda     #0              ; acc16 = nx ow_x + ny ow_y (ow = -1, 0, 1)
        sta     tmp1
        sta     tmp2
        ldy     #S_OWX
        lda     (ptr3),y
        ldx     #snx - sdx      ; (offset of snx from sdx, for the helper)
        jsr     owadd
        ldy     #S_OWY
        lda     (ptr3),y
        ldx     #sny - sdx
        jsr     owadd
        lda     tmp2            ; <= 0: out
        bmi     @out
        ora     tmp1
        beq     @out
        lda     svn+1           ; vn >= 0: out
        bpl     @out
        jmp     @twoway
@out:   lda     #$FF
        tax
        rts
@twoway:
        ; q = a + u tt (mulq14), the ball to q + n rr
        lda     tt
        ldx     tt+1
        mulw    sux
        jsr     q14
        ldy     #S_AX
        clc
        adc     (ptr3),y
        sta     sqx
        txa
        iny
        adc     (ptr3),y
        sta     sqx+1
        lda     tt
        ldx     tt+1
        mulw    suy
        jsr     q14
        ldy     #S_AY
        clc
        adc     (ptr3),y
        sta     sqy
        txa
        iny
        adc     (ptr3),y
        sta     sqy+1
        lda     snx
        ldx     snx+1
        mulw    srr
        jsr     q14
        clc
        adc     sqx
        ldy     #B_X
        sta     (ptr4),y
        txa
        adc     sqx+1
        iny
        sta     (ptr4),y
        lda     sny
        ldx     sny+1
        mulw    srr
        jsr     q14
        clc
        adc     sqy
        ldy     #B_Y
        sta     (ptr4),y
        txa
        adc     sqy+1
        iny
        sta     (ptr4),y
        lda     #128            ; place(): the fractions to the middle
        ldy     #B_XF
        sta     (ptr4),y
        iny
        sta     (ptr4),y
        lda     svn+1
        bmi     @bounce
        lda     #0              ; vn >= 0: touching, no bounce
        tax
        rts
@bounce:
        ldy     #S_E
        lda     (ptr3),y
        jsr     bounce8
        lda     #0              ; -vn
        sec
        sbc     svn
        tay
        lda     #0
        sbc     svn+1
        tax
        tya
        rts

; tmp2:tmp1 += (ow = A: -1, 0, 1) * the word at sdx + X
owadd:  cmp     #0
        beq     @z
        bmi     @m
        lda     tmp1
        clc
        adc     sdx,x
        sta     tmp1
        lda     tmp2
        adc     sdx+1,x
        sta     tmp2
@z:     rts
@m:     lda     tmp1
        sec
        sbc     sdx,x
        sta     tmp1
        lda     tmp2
        sbc     sdx+1,x
        sta     tmp2
        rts

; bounce(b, nx, ny, vn, e = A, fric 8): pb_phys.c's, for a wall
bounce8:
        sta     see
        lda     #0
        sta     see+1
        ; slow = -vn < SLOW  <=>  vn > -SLOW
        lda     #<(-SLOW)
        cmp     svn
        lda     #>(-SLOW)
        sbc     svn+1
        bvc     @v
        eor     #$80
@v:     bmi     @slow           ; -SLOW < vn
        lda     #<248
        sta     sk
        lda     #>248
        sta     sk+1
        jmp     @k
@slow:  lda     #0
        sta     see
        lda     #<256
        sta     sk
        lda     #>256
        sta     sk+1
@k:     ; en = mulq8(ee, vn)
        lda     see
        ldx     see+1
        mulw    svn
        lda     pr+1
        sta     sen
        lda     pr+2
        sta     sen+1
        ; tx = vx - mulq14(vn, nx)
        lda     svn
        ldx     svn+1
        mulw    snx
        jsr     q14
        sta     bq
        stx     bq+1
        ldy     #B_VX
        lda     (ptr4),y
        sec
        sbc     bq
        sta     stx_
        iny
        lda     (ptr4),y
        sbc     bq+1
        sta     stx_+1
        lda     svn
        ldx     svn+1
        mulw    sny
        jsr     q14
        sta     bq
        stx     bq+1
        ldy     #B_VY
        lda     (ptr4),y
        sec
        sbc     bq
        sta     sty_
        iny
        lda     (ptr4),y
        sbc     bq+1
        sta     sty_+1
        ; vx = mulq8(tx, k) - mulq14(en, nx)
        lda     sen
        ldx     sen+1
        mulw    snx
        jsr     q14
        sta     bq
        stx     bq+1
        lda     stx_
        ldx     stx_+1
        mulw    sk
        lda     pr+1
        sec
        sbc     bq
        ldy     #B_VX
        sta     (ptr4),y
        lda     pr+2
        sbc     bq+1
        iny
        sta     (ptr4),y
        lda     sen
        ldx     sen+1
        mulw    sny
        jsr     q14
        sta     bq
        stx     bq+1
        lda     sty_
        ldx     sty_+1
        mulw    sk
        lda     pr+1
        sec
        sbc     bq
        ldy     #B_VY
        sta     (ptr4),y
        lda     pr+2
        sbc     bq+1
        iny
        sta     (ptr4),y
        rts

; ---------------------------------------------------------------- flippers
; int16_t __fastcall__ flip_fast(Ball *b, const Flipper *f): pb_phys.c's
; hit_flipper() (the ball against a moving, tapered capsule; the surface's
; velocity w x r at the contact), the same arithmetic step for step: the
; same products, shifts, divisions and saturations, so the same results
; (make FLIPCHECK=1 runs both and logs any difference).
        .export _flip_fast
        .import _b0_flipper_tip, _isqrt32, _div_q14, _udiv32_16
        .import pusheax
        .importzp sreg

F_PX    = 1                     ; Flipper (pbz.h: FlipDef d; ang, w, held, tx, ty, tip_ang)
F_PY    = 3
F_LEN   = 5
F_R1    = 7
F_R2    = 9
F_ANG   = 15
F_W     = 17
F_TX    = 20
F_TY    = 22
F_TIP   = 24

.segment "B0BSS"
fb:     .word 0                 ; the ball, the flipper
ff:     .word 0
ftx:    .word 0
fty:    .word 0
fex:    .word 0
fey:    .word 0
fpx:    .word 0
fpy:    .word 0
fnum:   .res 4
fden:   .res 4
ft:     .word 0
fqx:    .word 0
fqy:    .word 0
frad:   .word 0
fdx:    .word 0
fdy:    .word 0
frr:    .word 0
fd:     .word 0
fnx:    .word 0
fny:    .word 0
fcx:    .word 0
fcy:    .word 0
fvcx:   .word 0
fvcy:   .word 0
frvx:   .word 0
frvy:   .word 0
fvn:    .word 0
fen:    .word 0
fk:     .word 0
fsx:    .word 0
fsy:    .word 0
fw:     .word 0
s32:    .res 4                  ; a 32-bit sum (sat16)

.segment "B0CODE"

; the flipper's word at offset -> A/X
.macro  fwd off
        ldy     #off+1
        lda     (ptr3),y
        tax
        dey
        lda     (ptr3),y
.endmacro
; A/X = mulq14(A/X, word w)
.macro  mq14 w
        mulw    w
        jsr     q14
.endmacro
; word d = word a - word b
.macro  sub16 dst, sa, sb
        lda     sa
        sec
        sbc     sb
        sta     dst
        lda     sa+1
        sbc     sb+1
        sta     dst+1
.endmacro
; word d = word a + A/X
.macro  addax dst, src
        clc
        adc     src
        sta     dst
        txa
        adc     src+1
        sta     dst+1
.endmacro

; |A/X| >= frr ? carry set
absge:  cpx     #$80
        bcc     @p
        sta     tmp1
        lda     #0
        sec
        sbc     tmp1
        sta     tmp1
        txa
        eor     #$FF
        adc     #0
        tax
        lda     tmp1
@p:     cmp     frr
        txa
        sbc     frr+1
        rts

; A/X = sat16(s32): to -32767..32767
sat16:  lda     s32+3
        bmi     @neg
        ora     s32+2
        bne     @max
        lda     s32+1
        bmi     @max
        lda     s32
        ldx     s32+1
        rts
@max:   lda     #<32767
        ldx     #>32767
        rts
@neg:   cmp     #$FF
        bne     @min
        lda     s32+2
        cmp     #$FF
        bne     @min
        lda     s32+1
        bpl     @min
        cmp     #$80            ; -32768 -> -32767
        bne     @ok
        lda     s32
        bne     @ok
@min:   lda     #<(-32767)
        ldx     #>(-32767)
        rts
@ok:    lda     s32
        ldx     s32+1
        rts

; s32 = word (sign-extended) at A/X ... helpers: s32 = A/X; s32 += A/X; s32 -= A/X
s32set: sta     s32
        stx     s32+1
        txa
        ora     #$7F
        bmi     @n
        lda     #0
@n:     sta     s32+2
        sta     s32+3
        rts
s32add: ldy     #0
        cpx     #$80
        bcc     @p
        dey
@p:     clc
        adc     s32
        sta     s32
        txa
        adc     s32+1
        sta     s32+1
        tya
        adc     s32+2
        sta     s32+2
        tya
        adc     s32+3
        sta     s32+3
        rts
s32sub: ldy     #0
        cpx     #$80
        bcc     @p
        dey
@p:     sta     tmp1
        stx     tmp2
        sty     tmp3
        lda     s32
        sec
        sbc     tmp1
        sta     s32
        lda     s32+1
        sbc     tmp2
        sta     s32+1
        lda     s32+2
        sbc     tmp3
        sta     s32+2
        lda     s32+3
        sbc     tmp3
        sta     s32+3
        rts

_flip_fast:
        sta     ptr3            ; f
        stx     ptr3+1
        sta     ff
        stx     ff+1
        jsr     popax           ; b
        sta     ptr4
        stx     ptr4+1
        sta     fb
        stx     fb+1
        ; the tip (cached until the flipper moves: pb_flipper_tip)
        ldy     #F_TIP
        lda     (ptr3),y
        ldy     #F_ANG
        cmp     (ptr3),y
        bne     @tipc
        ldy     #F_TIP+1
        lda     (ptr3),y
        ldy     #F_ANG+1
        cmp     (ptr3),y
        beq     @tipok
@tipc:  lda     ff
        ldx     ff+1
        jsr     pushax
        lda     #<ftx
        ldx     #>ftx
        jsr     pushax
        lda     #<fty
        ldx     #>fty
        jsr     _b0_flipper_tip
        lda     ff
        sta     ptr3
        lda     ff+1
        sta     ptr3+1
        lda     fb
        sta     ptr4
        lda     fb+1
        sta     ptr4+1
@tipok: fwd     F_TX
        sta     ftx
        stx     ftx+1
        fwd     F_TY
        sta     fty
        stx     fty+1
        fwd     F_PX
        sta     fpx
        stx     fpx+1
        fwd     F_PY
        sta     fpy
        stx     fpy+1
        fwd     F_W
        sta     fw
        stx     fw+1
        sub16   fex, ftx, fpx
        sub16   fey, fty, fpy
        ; num = (b.x - px) ex + (b.y - py) ey
        ldy     #B_X
        lda     (ptr4),y
        sec
        sbc     fpx
        pha
        iny
        lda     (ptr4),y
        sbc     fpx+1
        tax
        pla
        mulw    fex
        pr2acc
        ldy     #B_Y
        lda     (ptr4),y
        sec
        sbc     fpy
        pha
        iny
        lda     (ptr4),y
        sbc     fpy+1
        tax
        pla
        mulw    fey
        jsr     addacc
        ldx     #3
@n:     lda     pr,x
        sta     fnum,x
        dex
        bpl     @n
        ; den = len len
        fwd     F_LEN
        sta     ma
        stx     ma+1
        sta     mb
        stx     mb+1
        jsr     smul_mm
        ldx     #3
@d:     lda     pr,x
        sta     fden,x
        dex
        bpl     @d
        ; t = num <= 0 ? 0 : num >= den ? 16384 : udiv32_16(num << 8, den >> 6)
        lda     fnum+3
        bmi     @t0
        ora     fnum+2
        ora     fnum+1
        ora     fnum
        beq     @t0
        lda     fnum
        cmp     fden
        lda     fnum+1
        sbc     fden+1
        lda     fnum+2
        sbc     fden+2
        lda     fnum+3
        sbc     fden+3
        bcc     @tdiv
        lda     #<16384
        ldx     #>16384
        jmp     @tset
@t0:    lda     #0
        tax
        jmp     @tset
@tdiv:  ; eax = num << 8 (num < den < 2^24): low = num0 << 8, high = num2:num1
        lda     fnum+2
        sta     sreg+1
        lda     fnum+1
        sta     sreg
        lda     #0
        ldx     fnum
        jsr     pusheax
        ; den >> 6 (16 bits)
        lda     fden
        sta     tmp1
        lda     fden+1
        sta     tmp2
        lda     fden+2
        ldx     #6
@ds:    lsr
        ror     tmp2
        ror     tmp1
        dex
        bne     @ds
        lda     tmp1
        ldx     tmp2
        jsr     _udiv32_16
@tset:  sta     ft
        stx     ft+1
        ; q = p + mulq14(t, e)
        lda     ft
        ldx     ft+1
        mq14    fex
        addax   fqx, fpx
        lda     ft
        ldx     ft+1
        mq14    fey
        addax   fqy, fpy
        ; rad = r1 + mulq14(r2 - r1, t)
        ldy     #F_R2
        lda     (ptr3),y
        ldy     #F_R1
        sec
        sbc     (ptr3),y
        sta     tmp3
        ldy     #F_R2+1
        lda     (ptr3),y
        ldy     #F_R1+1
        sbc     (ptr3),y
        tax
        lda     tmp3
        mq14    ft
        ldy     #F_R1
        clc
        adc     (ptr3),y
        sta     frad
        txa
        iny
        adc     (ptr3),y
        sta     frad+1
        ; d = b - q; rr = PB_R + rad
        ldy     #B_X
        lda     (ptr4),y
        sec
        sbc     fqx
        sta     fdx
        iny
        lda     (ptr4),y
        sbc     fqx+1
        sta     fdx+1
        ldy     #B_Y
        lda     (ptr4),y
        sec
        sbc     fqy
        sta     fdy
        iny
        lda     (ptr4),y
        sbc     fqy+1
        sta     fdy+1
        lda     frad
        clc
        adc     #<PB_R
        sta     frr
        lda     frad+1
        adc     #>PB_R
        sta     frr+1
        lda     fdx
        ldx     fdx+1
        jsr     absge
        bcs     @far
        lda     fdy
        ldx     fdy+1
        jsr     absge
        bcc     @near
@far:   lda     #$FF
        tax
        rts
@near:
        ; d2 = dx dx + dy dy (unsigned 32) >= rr rr: no contact
        lda     fdx
        ldx     fdx+1
        mulw    fdx
        pr2acc
        lda     fdy
        ldx     fdy+1
        mulw    fdy
        jsr     addacc
        ldx     #3
@a:     lda     pr,x
        sta     fnum,x          ; (fnum: d2 now)
        dex
        bpl     @a
        lda     frr
        ldx     frr+1
        mulw    frr
        lda     fnum
        cmp     pr
        lda     fnum+1
        sbc     pr+1
        lda     fnum+2
        sbc     pr+2
        lda     fnum+3
        sbc     pr+3
        bcc     @hit
@miss:  lda     #$FF
        tax
        rts
@hit:   lda     fnum+2
        sta     sreg
        lda     fnum+3
        sta     sreg+1
        lda     fnum
        ldx     fnum+1
        jsr     _isqrt32
        sta     fd
        stx     fd+1
        ora     fd+1
        bne     @unit
        ; dead centre: n = (-ey, ex) / len, the way the flipper moves
        lda     fey
        ldx     fey+1
        jsr     pushax
        fwd     F_LEN
        jsr     _div_q14
        sta     tmp3
        stx     tmp4
        lda     #0
        sec
        sbc     tmp3
        sta     fnx
        lda     #0
        sbc     tmp4
        sta     fnx+1
        lda     fex
        ldx     fex+1
        jsr     pushax
        lda     ff
        sta     ptr3
        lda     ff+1
        sta     ptr3+1
        fwd     F_LEN
        jsr     _div_q14
        sta     fny
        stx     fny+1
        lda     fw+1
        bpl     @nb
        sub16   fnx, zero16, fnx
        sub16   fny, zero16, fny
        jmp     @nb
@unit:  lda     fdx
        ldx     fdx+1
        jsr     pushax
        lda     fd
        ldx     fd+1
        jsr     _div_q14
        sta     fnx
        stx     fnx+1
        lda     fdy
        ldx     fdy+1
        jsr     pushax
        lda     fd
        ldx     fd+1
        jsr     _div_q14
        sta     fny
        stx     fny+1
@nb:    lda     fb
        sta     ptr4
        lda     fb+1
        sta     ptr4+1
        ; c = q + n rad
        lda     fnx
        ldx     fnx+1
        mq14    frad
        addax   fcx, fqx
        lda     fny
        ldx     fny+1
        mq14    frad
        addax   fcy, fqy
        ; vcx = -(int16)(mul16(w, cy - py) >> 10); vcy = (int16)(mul16(w, cx - px) >> 10)
        sub16   tmp1w, fcy, fpy
        lda     fw
        ldx     fw+1
        mulw    tmp1w
        jsr     q10
        sta     tmp3
        stx     tmp4
        lda     #0
        sec
        sbc     tmp3
        sta     fvcx
        lda     #0
        sbc     tmp4
        sta     fvcx+1
        sub16   tmp1w, fcx, fpx
        lda     fw
        ldx     fw+1
        mulw    tmp1w
        jsr     q10
        sta     fvcy
        stx     fvcy+1
        ; place(b, q + n rr)
        lda     fnx
        ldx     fnx+1
        mq14    frr
        clc
        adc     fqx
        ldy     #B_X
        sta     (ptr4),y
        txa
        adc     fqx+1
        iny
        sta     (ptr4),y
        lda     fny
        ldx     fny+1
        mq14    frr
        clc
        adc     fqy
        ldy     #B_Y
        sta     (ptr4),y
        txa
        adc     fqy+1
        iny
        sta     (ptr4),y
        lda     #128
        ldy     #B_XF
        sta     (ptr4),y
        iny
        sta     (ptr4),y
        ; rv = sat16(v - vc)
        ldy     #B_VX
        lda     (ptr4),y
        tax
        iny
        lda     (ptr4),y
        sta     tmp1
        txa
        ldx     tmp1
        jsr     s32set
        lda     fvcx
        ldx     fvcx+1
        jsr     s32sub
        jsr     sat16
        sta     frvx
        stx     frvx+1
        ldy     #B_VY
        lda     (ptr4),y
        tax
        iny
        lda     (ptr4),y
        sta     tmp1
        txa
        ldx     tmp1
        jsr     s32set
        lda     fvcy
        ldx     fvcy+1
        jsr     s32sub
        jsr     sat16
        sta     frvy
        stx     frvy+1
        ; vn = (rvx nx + rvy ny) >> 14
        lda     frvx
        ldx     frvx+1
        mulw    fnx
        pr2acc
        lda     frvy
        ldx     frvy+1
        mulw    fny
        jsr     addacc
        jsr     q14
        sta     fvn
        stx     fvn+1
        cpx     #$80
        bcs     @app
        lda     #0              ; vn >= 0: touching, no bounce
        tax
        rts
@app:   ; e = |w| > 256 ? 141 : 77; slow = -vn < SLOW: ee 0, k 256; else ee e, k 248
        lda     #<(-SLOW)
        cmp     fvn
        lda     #>(-SLOW)
        sbc     fvn+1
        bvc     @v
        eor     #$80
@v:     bmi     @slow           ; -SLOW < vn
        lda     #<248
        sta     fk
        lda     #0
        sta     fk+1
        ldx     #77             ; |w| > 256?
        lda     fw+1
        bpl     @wp
        ; w < 0: |w| > 256 <=> w < -256
        lda     fw
        cmp     #<(-256)
        lda     fw+1
        sbc     #>(-256)
        bcs     @ee
        ldx     #141
        bne     @ee
@wp:    lda     #<256
        cmp     fw
        lda     #>256
        sbc     fw+1
        bcs     @ee             ; w <= 256
        ldx     #141
@ee:    txa
        ldx     #0
        jmp     @en
@slow:  lda     #<256
        sta     fk
        lda     #>256
        sta     fk+1
        lda     #0
        tax
@en:    ; en = mulq8(ee, vn)
        mulw    fvn
        lda     pr+1
        sta     fen
        lda     pr+2
        sta     fen+1
        ; s = rv - mulq14(vn, n)
        lda     fvn
        ldx     fvn+1
        mq14    fnx
        sta     tmp3
        stx     tmp4
        lda     frvx
        sec
        sbc     tmp3
        sta     fsx
        lda     frvx+1
        sbc     tmp4
        sta     fsx+1
        lda     fvn
        ldx     fvn+1
        mq14    fny
        sta     tmp3
        stx     tmp4
        lda     frvy
        sec
        sbc     tmp3
        sta     fsy
        lda     frvy+1
        sbc     tmp4
        sta     fsy+1
        ; v = sat16(vc + mulq8(s, k) - mulq14(en, n))
        lda     fvcx
        ldx     fvcx+1
        jsr     s32set
        lda     fsx
        ldx     fsx+1
        mulw    fk
        lda     pr+1
        ldx     pr+2
        jsr     s32add
        lda     fen
        ldx     fen+1
        mq14    fnx
        jsr     s32sub
        jsr     sat16
        ldy     #B_VX
        sta     (ptr4),y
        txa
        iny
        sta     (ptr4),y
        lda     fvcy
        ldx     fvcy+1
        jsr     s32set
        lda     fsy
        ldx     fsy+1
        mulw    fk
        lda     pr+1
        ldx     pr+2
        jsr     s32add
        lda     fen
        ldx     fen+1
        mq14    fny
        jsr     s32sub
        jsr     sat16
        ldy     #B_VY
        sta     (ptr4),y
        txa
        iny
        sta     (ptr4),y
        lda     #0              ; return -vn
        sec
        sbc     fvn
        tay
        lda     #0
        sbc     fvn+1
        tax
        tya
        rts

; A/X = (pr >> 10): the product's bits 10..25
q10:    lda     pr+1
        sta     tmp1
        lda     pr+2
        sta     tmp2
        lda     pr+3
        lsr
        ror     tmp2
        ror     tmp1
        lsr
        ror     tmp2
        ror     tmp1
        lda     tmp1
        ldx     tmp2
        rts

.segment "B0BSS"
tmp1w:  .word 0
.segment "B0RO"
zero16: .word 0

; int16_t __fastcall__ sin_q14(int16_t a): A (1/4096 rad) -> Q14, zmath.c's
; C step for step: u = a * 2608 (1024 steps a turn, Q16), the quarter
; table at i = u >> 16 (mod 1024), interpolated by f = (u >> 8) & 255 with
; a 16-bit product, the sign by the quadrant
        .export _sin_q14
        .import _sin_quarter

.segment "B0BSS"
sq:     .byte 0
sf:     .byte 0
squad:  .byte 0
ss0:    .word 0
ss1:    .word 0

.segment "B0CODE"
_sin_q14:
        sta     ma
        stx     ma+1
        lda     #<2608
        sta     mb
        lda     #>2608
        sta     mb+1
        jsr     smul_mm
        lda     pr+1
        sta     sf
        lda     pr+2
        sta     sq
        lda     pr+3
        and     #3
        sta     squad
        lsr                     ; quad & 1: from the top of the table
        bcc     @up
        ; s0 = T[256 - q], s1 = T[255 - q]
        lda     #0
        sec
        sbc     sq              ; 256 - q (q = 0: 256)
        tax
        lda     #1
        sbc     #0              ; (the borrow: high byte 1 when q = 0)
        jsr     tget            ; A:X index -> ss0
        sta     ss0
        stx     ss0+1
        lda     #255
        sec
        sbc     sq
        tax
        lda     #0
        jsr     tget
        sta     ss1
        stx     ss1+1
        jmp     @mix
@up:    ldx     sq
        lda     #0
        jsr     tget
        sta     ss0
        stx     ss0+1
        ldx     sq
        inx
        lda     #0
        bne     @u1
        cpx     #0
        bne     @u1
        lda     #1              ; q + 1 = 256
@u1:    jsr     tget
        sta     ss1
        stx     ss1+1
@mix:   ; s0 += ((s1 - s0) * f) >> 8
        lda     ss1
        sec
        sbc     ss0
        sta     ma
        lda     ss1+1
        sbc     ss0+1
        sta     ma+1
        lda     sf
        sta     mb
        lda     #0
        sta     mb+1
        jsr     smul_mm
        lda     pr+1
        clc
        adc     ss0
        sta     ss0
        lda     pr+2
        adc     ss0+1
        sta     ss0+1
        lda     squad
        and     #2
        beq     @pos
        lda     #0
        sec
        sbc     ss0
        tay
        lda     #0
        sbc     ss0+1
        tax
        tya
        rts
@pos:   lda     ss0
        ldx     ss0+1
        rts

; A/X = sin_quarter[A:X] (index high byte A, low byte X)
tget:   stx     tmp1
        sta     tmp2
        asl     tmp1
        rol     tmp2
        lda     tmp1
        clc
        adc     #<_sin_quarter
        sta     tmp1
        lda     tmp2
        adc     #>_sin_quarter
        sta     tmp2
        ldy     #1
        lda     (tmp1),y
        tax
        dey
        lda     (tmp1),y
        rts

; ---------------------------------------------------------------- substeps
; void substeps(void): pb_world_step's substep loop (pb_phys.c fills the
; sub_* below first): n times, the moving flippers' flipper_step (a
; flipper at rest when the step starts stays so: the others are left out),
; move_ball for each ball in play with this substep's gravity (gq, + 1 for
; the first gr), and the balls against each other.
        .export _substeps, _sub_w, _sub_n, _sub_gq, _sub_gr, _sub_fl, _sub_nfl, _sub_bl, _sub_nb
        .import _flipper_step, _pb_hit_balls

.segment "B0BSS"
_sub_w:   .word 0
_sub_n:   .byte 0
_sub_gq:  .word 0
_sub_gr:  .byte 0
_sub_fl:  .res 6                ; the moving flippers
_sub_nfl: .byte 0
_sub_bl:  .res 8                ; the balls
_sub_nb:  .byte 0
sk_:    .byte 0
si_:    .byte 0
sj_:    .byte 0
sgrav:  .word 0

BALL_STATE = 1                  ; Ball.state; BS_PLAY = 0

.segment "B0CODE"
_substeps:
        lda     #0
        sta     sk_
@k:     lda     sk_
        cmp     _sub_n
        bne     @go
        rts
@go:    ; grav = gq + (k < gr)
        lda     _sub_gq
        sta     sgrav
        lda     _sub_gq+1
        sta     sgrav+1
        lda     sk_
        cmp     _sub_gr
        bcs     @g
        inc     sgrav
        bne     @g
        inc     sgrav+1
@g:     ; the moving flippers
        lda     #0
        sta     si_
@f:     lda     si_
        cmp     _sub_nfl
        beq     @fd
        lda     _sub_w
        ldx     _sub_w+1
        jsr     pushax
        lda     si_
        asl
        tay
        lda     _sub_fl,y
        ldx     _sub_fl+1,y
        jsr     _flipper_step
        inc     si_
        jmp     @f
@fd:    ; the balls in play
        lda     #0
        sta     si_
@b:     lda     si_
        cmp     _sub_nb
        beq     @bd
        asl
        tay
        lda     _sub_bl,y
        sta     ptr4
        lda     _sub_bl+1,y
        sta     ptr4+1
        ldy     #BALL_STATE
        lda     (ptr4),y
        bne     @bn
        lda     _sub_w
        ldx     _sub_w+1
        jsr     pushax
        lda     ptr4
        ldx     ptr4+1
        jsr     pushax
        lda     sgrav
        ldx     sgrav+1
        jsr     _move_ball
@bn:    inc     si_
        jmp     @b
@bd:    ; the balls against each other
        lda     _sub_nb
        cmp     #2
        bcc     @next
        lda     #0
        sta     si_
@p1:    lda     si_
        clc
        adc     #1
        sta     sj_
@p2:    lda     sj_
        cmp     _sub_nb
        bcs     @p1n
        lda     si_
        asl
        tay
        lda     _sub_bl,y
        sta     ptr4
        lda     _sub_bl+1,y
        sta     ptr4+1
        ldy     #BALL_STATE
        lda     (ptr4),y
        bne     @p2n
        lda     sj_
        asl
        tay
        lda     _sub_bl,y
        sta     ptr3
        lda     _sub_bl+1,y
        sta     ptr3+1
        ldy     #BALL_STATE
        lda     (ptr3),y
        bne     @p2n
        lda     ptr4
        ldx     ptr4+1
        jsr     pushax
        lda     ptr3
        ldx     ptr3+1
        jsr     _pb_hit_balls
@p2n:   inc     sj_
        jmp     @p2
@p1n:   inc     si_
        lda     si_
        cmp     _sub_nb
        bcc     @p1
@next:  inc     sk_
        jmp     @k

; int16_t __fastcall__ div625(int16_t v): pb_phys.c's v / 625 truncated
; towards 0, as (|v| * 13422) >> 23 (exact for |v| < 32768; |v| here is
; at most the speed cap, 28000)
        .export _div625
.segment "B0BSS"
dsg:    .byte 0
.segment "B0CODE"
_div625:
        stx     dsg
        cpx     #$80
        bcc     @p
        eor     #$FF
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
@p:     sta     ma
        stx     ma+1
        lda     #<13422
        sta     mb
        lda     #>13422
        sta     mb+1
        jsr     smul_mm
        lda     pr+2            ; >> 23: pr+3 << 1 | pr+2 >> 7 (< 64)
        asl
        lda     pr+3
        rol
        ldx     #0
        bit     dsg
        bpl     @r
        eor     #$FF
        clc
        adc     #1
        beq     @r
        ldx     #$FF
@r:     rts

; ---------------------------------------------------------------- the step
; void __fastcall__ b0_world_step(World *w): pb_world_step (pb_phys.c had
; the C; the same, step for step):
;   each ball: nocatch counts down; in play: the rolling resistance
;   (v -= v / 625), its speed from above (max + min / 2 + the gravity's
;   step, at most 32767): the fastest;
;   each moving flipper (w != 0 or not at its target): to the substeps'
;   list; if a ball in play is within its reach + a step's travel, the
;   tip's speed counts (len + r2) * 21 / 2;
;   n = 1 + (fast + tip) / 1350 (by subtraction, at most 40) with a ball in
;   play; the substep's constants from sub_tab[n]; the move tables
;   (pb_mb_prepare) for a new table; the substeps; then the balls on a
;   ramp (pb_ramp_step), in the saucer (hold counts down, at rest), in the
;   lane (at rest); the spinners (pb_phys.c's spinners) when one turns.
        .export _b0_world_step
        .import _sub_tab, _sub_acc_up, _sub_acc_down, _sub_kang, _sub_n1
        .import _mb_t, _pb_mb_prepare, _pb_ramp_step, _spinners
        .importzp ptr1, ptr2

W_BALLS = 2                     ; World (pb_phys.c checks these)
W_NB    = 106
W_FLIP  = 107
W_NF    = 185
W_SPIN  = 186
W_POWER = 532
W_SUBST = 534
B_STATE = 1
B_HOLD  = 19
B_NOC   = 21
F_SIDE_ = 0
F_REST  = 11
F_UP    = 13
F_HELD  = 19
SIZE_B  = 26
SIZE_F  = 26
BS_LANE = 1
BS_RAMP = 2
BS_SAUCER = 3
GRAVSTEP = 240

.segment "B0BSS"
ws_w:   .word 0
ws_fast: .word 0
ws_tip: .word 0
ws_live: .byte 0
ws_i:   .byte 0
ws_j:   .byte 0
ws_n:   .byte 0
ws_j2:  .byte 0
ws_ax:  .word 0
ws_ay:  .word 0
ws_r:   .word 0
ws_px:  .word 0
ws_py:  .word 0

.segment "B0CODE"

; ptr3 = &w->balls[A] (A < 4)
ballp:  tax
        lda     ws_w
        clc
        adc     #W_BALLS
        sta     ptr3
        lda     ws_w+1
        adc     #0
        sta     ptr3+1
@m:     dex
        bmi     @r
        lda     ptr3
        clc
        adc     #SIZE_B
        sta     ptr3
        bcc     @m
        inc     ptr3+1
        bne     @m
@r:     rts

; ptr4 = &w->flip[A]
flipp:  tax
        lda     ws_w
        clc
        adc     #W_FLIP
        sta     ptr4
        lda     ws_w+1
        adc     #0
        sta     ptr4+1
@m:     dex
        bmi     @r
        lda     ptr4
        clc
        adc     #SIZE_F
        sta     ptr4
        bcc     @m
        inc     ptr4+1
        bne     @m
@r:     rts

; A/X = |A/X| (int16)
absax:  cpx     #$80
        bcc     @r
        eor     #$FF
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
@r:     rts

; the byte at w + Y -> A
.macro  wb  off
        ldy     #off
        lda     (ptr2),y
.endmacro

_b0_world_step:
        sta     ws_w
        stx     ws_w+1
        lda     #0
        sta     ws_fast
        sta     ws_fast+1
        sta     ws_tip
        sta     ws_tip+1
        sta     ws_live
        ; ---- the balls
        sta     ws_i
@ball:  lda     ws_w
        sta     ptr2
        lda     ws_w+1
        sta     ptr2+1
        wb      W_NB
        cmp     ws_i
        beq     @bd1
        bpl     @bgo
@bd1:   jmp     @bdone
@bgo:   lda     ws_i
        jsr     ballp
        ; nocatch > 0: -1
        ldy     #B_NOC+1
        lda     (ptr3),y
        bmi     @nc
        dey
        ora     (ptr3),y
        beq     @nc
        lda     (ptr3),y
        sec
        sbc     #1
        sta     (ptr3),y
        iny
        lda     (ptr3),y
        sbc     #0
        sta     (ptr3),y
@nc:    ldy     #B_STATE
        lda     (ptr3),y
        beq     @inplay
        jmp     @bnext
@inplay:
        inc     ws_live
        ; v -= div625(v)
        ldy     #B_VX
        jsr     resist
        ldy     #B_VY
        jsr     resist
        ; s = max + min / 2 + GRAVSTEP (unsigned), at most 32767
        ldy     #B_VX+1
        lda     (ptr3),y
        tax
        dey
        lda     (ptr3),y
        jsr     absax
        sta     ws_ax
        stx     ws_ax+1
        ldy     #B_VY+1
        lda     (ptr3),y
        tax
        dey
        lda     (ptr3),y
        jsr     absax
        sta     ws_ay
        stx     ws_ay+1
        lda     ws_ay           ; ax > ay ?
        cmp     ws_ax
        lda     ws_ay+1
        sbc     ws_ax+1
        bcs     @aylarger       ; ay >= ax
        lda     ws_ay+1         ; ax + ay / 2
        lsr
        sta     ws_r+1
        lda     ws_ay
        ror
        clc
        adc     ws_ax
        sta     ws_r
        lda     ws_r+1
        adc     ws_ax+1
        sta     ws_r+1
        jmp     @s
@aylarger:
        lda     ws_ax+1         ; ay + ax / 2
        lsr
        sta     ws_r+1
        lda     ws_ax
        ror
        clc
        adc     ws_ay
        sta     ws_r
        lda     ws_r+1
        adc     ws_ay+1
        sta     ws_r+1
@s:     lda     ws_r
        clc
        adc     #<GRAVSTEP
        sta     ws_r
        lda     ws_r+1
        adc     #>GRAVSTEP
        sta     ws_r+1
        bmi     @cap            ; > 32767 (unsigned, at most ~65K: bit 15)
        bcc     @nocap
@cap:   lda     #$FF
        sta     ws_r
        lda     #$7F
        sta     ws_r+1
@nocap: lda     ws_fast         ; s > fast?
        cmp     ws_r
        lda     ws_fast+1
        sbc     ws_r+1
        bcs     @bnext
        lda     ws_r
        sta     ws_fast
        lda     ws_r+1
        sta     ws_fast+1
@bnext: inc     ws_i
        jmp     @ball
@bdone: ; ---- the flippers
        lda     #0
        sta     _sub_nfl
        sta     ws_i
@flip:  lda     ws_w
        sta     ptr2
        lda     ws_w+1
        sta     ptr2+1
        wb      W_NF
        cmp     ws_i
        bne     @f1
        jmp     @fdone
@f1:    bmi     @f1d
        jmp     @f2
@f1d:   jmp     @fdone
@f2:    inc     ptr2+1          ; (w + 512: the field is past 255)
        inc     ptr2+1
        wb      W_POWER - 512
        dec     ptr2+1
        dec     ptr2+1
        sta     tmp4
        lda     ws_i
        jsr     flipp
        ; target = held && power ? up : rest
        ldy     #F_REST
        lda     tmp4
        beq     @t
        ldy     #F_HELD
        lda     (ptr4),y
        beq     @trest
        ldy     #F_UP
        bne     @t
@trest: ldy     #F_REST
@t:     lda     (ptr4),y
        sta     ws_r
        iny
        lda     (ptr4),y
        sta     ws_r+1
        ; moving: w != 0 || ang != target
        ldy     #F_W
        lda     (ptr4),y
        iny
        ora     (ptr4),y
        bne     @mov
        ldy     #F_ANG
        lda     (ptr4),y
        cmp     ws_r
        bne     @mov
        iny
        lda     (ptr4),y
        cmp     ws_r+1
        bne     @mov
        jmp     @fnext
@mov:   lda     _sub_nfl        ; to the substeps' list
        asl
        tay
        lda     ptr4
        sta     _sub_fl,y
        lda     ptr4+1
        sta     _sub_fl+1,y
        inc     _sub_nfl
        ; reach = len + r1 + PB_R + 4480
        ldy     #F_LEN
        lda     (ptr4),y
        ldy     #F_R1
        clc
        adc     (ptr4),y
        sta     ws_r
        ldy     #F_LEN+1
        lda     (ptr4),y
        ldy     #F_R1+1
        adc     (ptr4),y
        sta     ws_r+1
        lda     ws_r
        clc
        adc     #<(PB_R + 4480)
        sta     ws_r
        lda     ws_r+1
        adc     #>(PB_R + 4480)
        sta     ws_r+1
        ldy     #F_PX
        lda     (ptr4),y
        sta     ws_px
        iny
        lda     (ptr4),y
        sta     ws_px+1
        ldy     #F_PY
        lda     (ptr4),y
        sta     ws_py
        iny
        lda     (ptr4),y
        sta     ws_py+1
        ; a ball in play within reach?
        lda     #0
        sta     ws_j
@fb:    wb      W_NB
        cmp     ws_j
        beq     @fb1
        bpl     @fb2
@fb1:   jmp     @fnext
@fb2:
        lda     ws_j
        jsr     ballp
        ldy     #B_STATE
        lda     (ptr3),y
        bne     @fbn
        ldy     #B_X
        lda     (ptr3),y
        sec
        sbc     ws_px
        pha
        iny
        lda     (ptr3),y
        sbc     ws_px+1
        tax
        pla
        jsr     absax
        jsr     ltreach
        bcs     @fbn
        ldy     #B_Y
        lda     (ptr3),y
        sec
        sbc     ws_py
        pha
        iny
        lda     (ptr3),y
        sbc     ws_py+1
        tax
        pla
        jsr     absax
        jsr     ltreach
        bcc     @found
@fbn:   inc     ws_j
        jmp     @fb
@found: ; tp = (mul16(len + r2, 21) >> 1); tp > tip: tip = tp
        ldy     #F_LEN
        lda     (ptr4),y
        ldy     #F_R2
        clc
        adc     (ptr4),y
        sta     ma
        ldy     #F_LEN+1
        lda     (ptr4),y
        ldy     #F_R2+1
        adc     (ptr4),y
        sta     ma+1
        lda     #21
        sta     mb
        lda     #0
        sta     mb+1
        jsr     smul_mm
        lda     pr+2            ; (pr >> 1), the low 16 bits
        lsr
        lda     pr+1
        ror
        sta     ws_ax+1
        lda     pr
        ror
        sta     ws_ax
        lda     ws_tip          ; tp > tip (signed)?
        cmp     ws_ax
        lda     ws_tip+1
        sbc     ws_ax+1
        bvc     @v
        eor     #$80
@v:     bpl     @fnext
        lda     ws_ax
        sta     ws_tip
        lda     ws_ax+1
        sta     ws_tip+1
@fnext: inc     ws_i
        jmp     @flip
@fdone: ; ---- n = 1 + (fast + tip) / 1350, at most 40, with a ball in play
        lda     ws_fast
        clc
        adc     ws_tip
        sta     ws_r
        lda     ws_fast+1
        adc     ws_tip+1
        sta     ws_r+1
        lda     #1
        sta     ws_n
        lda     ws_live
        beq     @ndone
@nl:    lda     #<1350          ; sp > 1350?
        cmp     ws_r
        lda     #>1350
        sbc     ws_r+1
        bcs     @ndone
        lda     ws_n
        cmp     #40
        bcs     @ndone
        lda     ws_r
        sec
        sbc     #<1350
        sta     ws_r
        lda     ws_r+1
        sbc     #>1350
        sta     ws_r+1
        inc     ws_n
        bne     @nl
@ndone: ldx     #0
        lda     ws_n
        cmp     #1
        bne     @n1
        inx
@n1:    stx     _sub_n1
        inc     ptr2+1
        inc     ptr2+1
        ldy     #W_SUBST - 512
        lda     ws_n
        sta     (ptr2),y
        dec     ptr2+1
        dec     ptr2+1
        ; sub_tab[n]: 12 bytes a row
        lda     ws_n
        asl
        asl
        sta     tmp1            ; n * 4
        asl
        clc
        adc     tmp1            ; n * 12 (n <= 40: < 256... 480: carry)
        sta     tmp1
        lda     #0
        rol
        sta     tmp2
        ; (n * 12 = n * 8 + n * 4: redo with 16 bits)
        lda     ws_n
        sta     tmp1
        lda     #0
        sta     tmp2
        asl     tmp1
        rol     tmp2
        asl     tmp1
        rol     tmp2            ; n * 4
        lda     tmp1
        sta     tmp3
        lda     tmp2
        sta     tmp4
        asl     tmp1
        rol     tmp2            ; n * 8
        lda     tmp1
        clc
        adc     tmp3
        sta     tmp1
        lda     tmp2
        adc     tmp4
        sta     tmp2            ; n * 12
        lda     tmp1
        clc
        adc     #<_sub_tab
        sta     ptr1
        lda     tmp2
        adc     #>_sub_tab
        sta     ptr1+1
        ldy     #0
        lda     (ptr1),y
        sta     _sub_acc_up
        iny
        lda     (ptr1),y
        sta     _sub_acc_up+1
        iny
        lda     (ptr1),y
        sta     _sub_acc_down
        iny
        lda     (ptr1),y
        sta     _sub_acc_down+1
        iny
        lda     (ptr1),y
        sta     _sub_kang
        iny
        lda     (ptr1),y
        sta     _sub_kang+1
        iny
        lda     (ptr1),y
        sta     _sub_kpos
        iny
        lda     (ptr1),y
        sta     _sub_kpos+1
        iny
        lda     (ptr1),y
        sta     _sub_gq
        iny
        lda     (ptr1),y
        sta     _sub_gq+1
        iny
        lda     (ptr1),y
        sta     _sub_gr         ; (0..39: its low byte)
        ; the move tables for a new table
        ldy     #1
        lda     (ptr2),y
        cmp     _mb_t+1
        bne     @prep
        dey
        lda     (ptr2),y
        cmp     _mb_t
        beq     @noprep
@prep:  lda     ws_w
        ldx     ws_w+1
        jsr     _pb_mb_prepare
@noprep:
        ; the substeps
        lda     ws_w
        sta     _sub_w
        sta     ptr2
        lda     ws_w+1
        sta     _sub_w+1
        sta     ptr2+1
        lda     ws_n
        sta     _sub_n
        wb      W_NB
        sta     _sub_nb
        lda     #0
        sta     ws_i
@bl:    lda     ws_i
        cmp     _sub_nb
        beq     @bld
        jsr     ballp
        lda     ws_i
        asl
        tay
        lda     ptr3
        sta     _sub_bl,y
        lda     ptr3+1
        sta     _sub_bl+1,y
        inc     ws_i
        bne     @bl
@bld:   jsr     _substeps
        ; ---- the balls on a ramp, in the saucer, in the lane
        lda     #0
        sta     ws_i
@after: lda     ws_i
        cmp     _sub_nb
        beq     @adone
        jsr     ballp
        ldy     #B_STATE
        lda     (ptr3),y
        cmp     #BS_RAMP
        bne     @sau
        lda     ws_w
        ldx     ws_w+1
        jsr     pushax
        lda     ptr3
        ldx     ptr3+1
        jsr     _pb_ramp_step
        jmp     @anext
@sau:   cmp     #BS_SAUCER
        bne     @lane
        ldy     #B_HOLD         ; hold--
        lda     (ptr3),y
        sec
        sbc     #1
        sta     (ptr3),y
        iny
        lda     (ptr3),y
        sbc     #0
        sta     (ptr3),y
        jmp     @rest
@lane:  cmp     #BS_LANE
        bne     @anext
@rest:  lda     #0
        ldy     #B_VX
        sta     (ptr3),y
        iny
        sta     (ptr3),y
        iny
        sta     (ptr3),y
        iny
        sta     (ptr3),y
@anext: inc     ws_i
        jmp     @after
@adone: ; ---- the spinners: C, when one turns (w > 0)
        lda     ws_w
        sta     ptr2
        lda     ws_w+1
        sta     ptr2+1
        ldy     #W_SPIN+3       ; spin[0].w
        lda     (ptr2),y
        bmi     @s1
        dey
        ora     (ptr2),y
        bne     @spin
@s1:    ldy     #W_SPIN+7       ; spin[1].w
        lda     (ptr2),y
        bmi     @s2
        dey
        ora     (ptr2),y
        bne     @spin
@s2:    rts
@spin:  lda     ws_w
        ldx     ws_w+1
        jmp     _spinners

; carry clear if A/X (>= 0) < ws_r (the reach)
ltreach:
        cmp     ws_r
        txa
        sbc     ws_r+1
        rts

; the ball's velocity word at Y (ptr3): v -= div625(v)
resist: sty     ws_j2
        iny
        lda     (ptr3),y
        tax
        dey
        lda     (ptr3),y
        jsr     _div625
        sta     tmp1
        stx     tmp2
        ldy     ws_j2
        lda     (ptr3),y
        sec
        sbc     tmp1
        sta     (ptr3),y
        iny
        lda     (ptr3),y
        sbc     tmp2
        sta     (ptr3),y
        rts
