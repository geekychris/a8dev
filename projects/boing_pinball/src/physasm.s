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
        .import _mb_cap, _mb_seg, _mb_circ, _mb_sens, _mb_flip, _mb_tail
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
        beq     mv_circ
        jmp     mv_sens

mv_next: jmp    mv_entry
mv_seg:   ldy     idx                     ; bx < x0 || bx > x1 || by < y0 || by > y1: next
        blt16y  bx, _mb_sx0, mv_next
        bgt16y  bx, _mb_sx1, mv_next
        blt16y  by, _mb_sy0, mv_next
        bgt16y  by, _mb_sy1, mv_next
        push_wb
        ldy     idx
        lda     _mb_sp,y
        ldx     _mb_sp+1,y
        jsr     _mb_seg
        inc     dirty
        jmp     mv_entry

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
        bcc     @inside         ; tt < len
@ends:  lda     #$00
        ldx     #$80
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
