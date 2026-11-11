; gather.s - world.c's gather() in assembly (world.c has the C it follows):
; every solid thing whose box roughly meets the box r, into cand_box[] /
; cand_body[]: the bodies (x and z tested first), then the room's grid
; cells r covers.
;
;   uint8_t gather_asm(const Box *r, const Body *self, uint8_t skip_npcbar)

        .macpack longbranch
        .export _gather_asm
        .import _w_body, _w_nbody, _w_gridbody, _cand_box, _cand_body, _grid, _gw, _gd
        .import popax
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

MAXCAND = 48
MAXH    = 6
MAXW    = 10
BODYSZ  = 30
B_PX    = 0
B_PY    = 2
B_PZ    = 4
B_SX    = 6
B_SY    = 8
B_SZ    = 10
B_USED  = 18
B_SOLID = 19
B_PLAYER = 23
B_NPCBAR = 24
B_HARM  = 26

.segment "BSS"
rx0:    .res 2                  ; the box r
ry0:    .res 2
rz0:    .res 2
rx1:    .res 2
ry1:    .res 2
rz1:    .res 2
skip:   .res 1
selfpl: .res 1                  ; self->player, self->harm (0 without self)
selfhm: .res 1
self:   .res 2
n:      .res 1
i:      .res 1
bx0:    .res 2
bz0:    .res 2
t0:     .res 2
gx0:    .res 1                  ; the grid cells' range
gx1:    .res 1
gy0:    .res 1
gy1:    .res 1
gz0:    .res 1
gz1:    .res 1
cx:     .res 1
cy:     .res 1
cz:     .res 1
cidx:   .res 2                  ; y * 100 + z * 10 + x
rowz:   .res 2

.segment "CODE"

; signed 16-bit compare: branches to `lab` when va > vb  (addresses)
.macro  bgt     va, vb, lab
        .local  ok
        sec
        lda     vb
        sbc     va
        lda     vb+1
        sbc     va+1
        bvc     ok
        eor     #$80
ok:     jmi     lab
.endmacro

_gather_asm:
        sta     skip
        jsr     popax           ; self
        sta     self
        stx     self+1
        sta     ptr2
        stx     ptr2+1
        lda     #0
        sta     selfpl
        sta     selfhm
        lda     self
        ora     self+1
        jeq     g_noself
        ldy     #B_PLAYER
        lda     (ptr2),y
        sta     selfpl
        ldy     #B_HARM
        lda     (ptr2),y
        sta     selfhm
g_noself:
        jsr     popax           ; r
        sta     ptr1
        stx     ptr1+1
        ldy     #11
g_cp:    lda     (ptr1),y
        sta     rx0,y
        dey
        jpl     g_cp
        lda     #0
        sta     n
        sta     i
        ; ptr2 = w_body, ptr3 = cand_box, ptr4 = cand_body
        lda     #<_w_body
        sta     ptr2
        lda     #>_w_body
        sta     ptr2+1
        lda     #<_cand_box
        sta     ptr3
        lda     #>_cand_box
        sta     ptr3+1
        lda     #<_cand_body
        sta     ptr4
        lda     #>_cand_body
        sta     ptr4+1
g_body:  lda     i
        cmp     _w_nbody
        jcc     g_b1
        jmp     g_grid
g_b1:    lda     n
        cmp     #MAXCAND
        jcc     g_b2
        jmp     g_grid
g_b2:    ldy     #B_USED
        lda     (ptr2),y
        jeq     g_next
        iny                     ; solid
        lda     (ptr2),y
        jeq     g_next
        lda     ptr2            ; o == self
        cmp     self
        jne     g_ns
        lda     ptr2+1
        cmp     self+1
        jeq     g_next
g_ns:    lda     skip
        jeq     g_nskip
        ldy     #B_NPCBAR
        lda     (ptr2),y
        jne     g_next
g_nskip: lda     selfpl          ; self->player && o->harm
        jeq     g_h2
        ldy     #B_HARM
        lda     (ptr2),y
        jne     g_next
g_h2:    lda     selfhm          ; self->harm && o->player
        jeq     g_xz
        ldy     #B_PLAYER
        lda     (ptr2),y
        jne     g_next
g_xz:    ; bx0 = px - (sx >> 1)
        ldy     #B_SX+1
        lda     (ptr2),y
        cmp     #$80
        ror
        sta     t0+1
        dey
        lda     (ptr2),y
        ror
        sta     t0
        ldy     #B_PX
        sec
        lda     (ptr2),y
        sbc     t0
        sta     bx0
        iny
        lda     (ptr2),y
        sbc     t0+1
        sta     bx0+1
        bgt     bx0, rx1, g_next             ; bx0 > rx1
        ; bx0 + sx < rx0  <=>  rx0 > bx0 + sx
        ldy     #B_SX
        clc
        lda     bx0
        adc     (ptr2),y
        sta     t0
        iny
        lda     bx0+1
        adc     (ptr2),y
        sta     t0+1
        bgt     rx0, t0, g_next
        jmp     g_z
g_next:  inc     i
        clc
        lda     ptr2
        adc     #BODYSZ
        sta     ptr2
        jcc     g_body
        inc     ptr2+1
        jmp     g_body
g_z:     ldy     #B_SZ+1
        lda     (ptr2),y
        cmp     #$80
        ror
        sta     t0+1
        dey
        lda     (ptr2),y
        ror
        sta     t0
        ldy     #B_PZ
        sec
        lda     (ptr2),y
        sbc     t0
        sta     bz0
        iny
        lda     (ptr2),y
        sbc     t0+1
        sta     bz0+1
        bgt     bz0, rz1, g_next
        ldy     #B_SZ
        clc
        lda     bz0
        adc     (ptr2),y
        sta     t0
        iny
        lda     bz0+1
        adc     (ptr2),y
        sta     t0+1
        bgt     rz0, t0, g_next
        ; the candidate's box: x0 y0 z0 x1 y1 z1
        ldy     #0
        lda     bx0
        sta     (ptr3),y
        iny
        lda     bx0+1
        sta     (ptr3),y
        ldy     #B_PY           ; y0 = py
        lda     (ptr2),y
        ldy     #2
        sta     (ptr3),y
        ldy     #B_PY+1
        lda     (ptr2),y
        ldy     #3
        sta     (ptr3),y
        ldy     #4              ; z0
        lda     bz0
        sta     (ptr3),y
        iny
        lda     bz0+1
        sta     (ptr3),y
        ldy     #6              ; x1 = bx0 + sx
        ldy     #B_SX
        clc
        lda     bx0
        adc     (ptr2),y
        ldy     #6
        sta     (ptr3),y
        ldy     #B_SX+1
        lda     bx0+1
        adc     (ptr2),y
        ldy     #7
        sta     (ptr3),y
        ldy     #B_PY           ; y1 = py + sy
        clc
        lda     (ptr2),y
        ldy     #B_SY
        adc     (ptr2),y
        ldy     #8
        sta     (ptr3),y
        sta     t0
        ldy     #B_PY+1
        lda     (ptr2),y
        ldy     #B_SY+1
        adc     (ptr2),y
        ldy     #9
        sta     (ptr3),y
        sta     t0+1
        ldy     #B_SZ           ; z1 = bz0 + sz
        clc
        lda     bz0
        adc     (ptr2),y
        ldy     #10
        sta     (ptr3),y
        ldy     #B_SZ+1
        lda     bz0+1
        adc     (ptr2),y
        ldy     #11
        sta     (ptr3),y
        ; c->y0 > r->y1 || c->y1 < r->y0 : skip (not counted)
        ldy     #B_PY
        lda     (ptr2),y
        sta     bx0             ; (bx0 reused: c->y0)
        iny
        lda     (ptr2),y
        sta     bx0+1
        bgt     bx0, ry1, g_skip
        bgt     ry0, t0, g_skip
        ; keep it
        ldy     #0
        lda     ptr2
        sta     (ptr4),y
        iny
        lda     ptr2+1
        sta     (ptr4),y
        inc     n
        clc
        lda     ptr3
        adc     #12
        sta     ptr3
        jcc     g_p3
        inc     ptr3+1
g_p3:    clc
        lda     ptr4
        adc     #2
        sta     ptr4
        jcc     g_skip
        inc     ptr4+1
g_skip:  jmp     g_next

; ---- the grid cells r covers ----
; ffloor(v) = v >> 11, arithmetic: the high byte >> 3
.macro  ffl     v, dst
        lda     v+1
        cmp     #$80
        ror
        cmp     #$80
        ror
        cmp     #$80
        ror
        sta     dst
.endmacro

g_grid:  ffl     rx0, gx0
        ffl     rx1, gx1
        ffl     ry0, gy0
        ffl     ry1, gy1
        ffl     rz0, gz0
        ffl     rz1, gz1
        ; clamp: lo < 0 -> 0, hi > max - 1 -> max - 1 (signed bytes)
        lda     gx0
        jpl     g_c1
        lda     #0
        sta     gx0
g_c1:    lda     gy0
        jpl     g_c2
        lda     #0
        sta     gy0
g_c2:    lda     gz0
        jpl     g_c3
        lda     #0
        sta     gz0
g_c3:    ldx     _gw
        dex
        stx     tmp1
        lda     gx1
        jmi     g_gdone          ; hi < 0: nothing
        cmp     tmp1
        jcc     g_c4
        stx     gx1
g_c4:    lda     gy1
        jmi     g_gdone
        cmp     #MAXH - 1
        jcc     g_c5
        lda     #MAXH - 1
        sta     gy1
g_c5:    ldx     _gd
        dex
        stx     tmp1
        lda     gz1
        jmi     g_gdone
        cmp     tmp1
        jcc     g_c6
        stx     gz1
g_c6:    lda     gy0
        sta     cy
g_yl:    lda     cy
        cmp     gy1
        jeq     g_y1
        jcs     g_gdone
g_y1:    lda     gz0
        sta     cz
g_zl:    lda     cz
        cmp     gz1
        jeq     g_z1
        jcs     g_ynext
g_z1:    ; rowz = grid + cy * 100 + cz * 10
        lda     cy
        asl
        asl                     ; * 4
        adc     cy              ; * 5
        asl
        asl                     ; * 20
        sta     tmp1
        asl
        asl                     ; * 80 (cy < 6: < 256)
        adc     tmp1            ; * 100
        sta     tmp1
        lda     cz
        asl
        sta     tmp2
        asl
        asl
        adc     tmp2            ; cz * 10
        clc
        adc     tmp1            ; + cy * 100 (up to 590: a 16-bit sum)
        sta     rowz
        lda     #0
        adc     #0
        sta     rowz+1
        clc
        lda     rowz
        adc     #<_grid
        sta     rowz
        lda     rowz+1
        adc     #>_grid
        sta     rowz+1
        lda     gx0
        sta     cx
g_xl:    lda     cx
        cmp     gx1
        jeq     g_x1
        jcs     g_znext
g_x1:    lda     n
        cmp     #MAXCAND
        jcs     g_gdone
        lda     rowz
        sta     ptr1
        lda     rowz+1
        sta     ptr1+1
        ldy     cx
        lda     (ptr1),y
        jeq     g_xnext
        ; a cell: x0 = cx << 11, x1 = x0 + 2048, and y, z the same
        ldy     #0
        lda     #0
        sta     (ptr3),y        ; x0 lo
        ldy     #4
        sta     (ptr3),y        ; z0 lo
        ldy     #2
        sta     (ptr3),y        ; y0 lo
        ldy     #6
        sta     (ptr3),y
        ldy     #8
        sta     (ptr3),y
        ldy     #10
        sta     (ptr3),y
        lda     cx
        asl
        asl
        asl
        ldy     #1
        sta     (ptr3),y
        adc     #8
        ldy     #7
        sta     (ptr3),y
        lda     cy
        asl
        asl
        asl
        ldy     #3
        sta     (ptr3),y
        adc     #8
        ldy     #9
        sta     (ptr3),y
        lda     cz
        asl
        asl
        asl
        ldy     #5
        sta     (ptr3),y
        adc     #8
        ldy     #11
        sta     (ptr3),y
        ldy     #0
        lda     #<_w_gridbody
        sta     (ptr4),y
        iny
        lda     #>_w_gridbody
        sta     (ptr4),y
        inc     n
        clc
        lda     ptr3
        adc     #12
        sta     ptr3
        jcc     g_q3
        inc     ptr3+1
g_q3:    clc
        lda     ptr4
        adc     #2
        sta     ptr4
        jcc     g_xnext
        inc     ptr4+1
g_xnext: inc     cx
        jmp     g_xl
g_znext: inc     cz
        jmp     g_zl
g_ynext: inc     cy
        jmp     g_yl
g_gdone: lda     n
        ldx     #0
        rts

; ---- move_axis's scan of the candidates (world.c has the C it follows) ----
;   fix cand_scan(uint8_t n)   with cs_start, cs_swept (Box), cs_axis (0..2),
;   cs_d (the move): the move allowed; cs_hit = the body hit (or 0)
; A candidate counts if it overlaps the swept box but not the start box
; (eps 2 each side); the allowed move stops at its face.
        .export _cand_scan, _cs_start, _cs_swept, _cs_axis, _cs_d, _cs_hit

.segment "BSS"
_cs_start: .res 12
_cs_swept: .res 12
_cs_axis:  .res 1
_cs_d:     .res 2
_cs_hit:   .res 2
cs_n:      .res 1
cs_i:      .res 1
allowed:   .res 2
slo:       .res 2
shi:       .res 2
lim:       .res 2
tq:        .res 2

.segment "CODE"

; overlap of box (ptr3) with the Box at `bx` (eps 2): branch to `no` if not
; a.x0 < b.x1 - 2 && a.x1 > b.x0 + 2 (and y, z)   [a = bx, b = (ptr3)]
.macro  ovl     bx, no
        .local  ax, nxt
        ldx     #0
ax:     ; b.lo + 2 < a.hi   (a.hi = bx + 6 + x, b.lo = (ptr3) + x)
        txa
        tay
        clc
        lda     (ptr3),y
        adc     #2
        sta     tq
        iny
        lda     (ptr3),y
        adc     #0
        sta     tq+1
        sec                     ; tq < a.hi  <=>  a.hi - tq > 0
        lda     tq
        sbc     bx+6,x
        lda     tq+1
        sbc     bx+7,x
        bvc     :+
        eor     #$80
:       jpl     no              ; tq >= a.hi
        ; a.lo < b.hi - 2   (b.hi = (ptr3) + 6 + x)
        txa
        clc
        adc     #6
        tay
        sec
        lda     (ptr3),y
        sbc     #2
        sta     tq
        iny
        lda     (ptr3),y
        sbc     #0
        sta     tq+1
        sec                     ; a.lo - tq < 0
        lda     bx,x
        sbc     tq
        lda     bx+1,x
        sbc     tq+1
        bvc     :+
        eor     #$80
:       jpl     no
        inx
        inx
        cpx     #6
        jne     ax
.endmacro

_cand_scan:
        sta     cs_n
        lda     _cs_d
        sta     allowed
        lda     _cs_d+1
        sta     allowed+1
        lda     #0
        sta     _cs_hit
        sta     _cs_hit+1
        sta     cs_i
        lda     _cs_axis
        asl
        tax
        lda     _cs_start,x
        sta     slo
        lda     _cs_start+1,x
        sta     slo+1
        lda     _cs_start+6,x
        sta     shi
        lda     _cs_start+7,x
        sta     shi+1
        lda     #<_cand_box
        sta     ptr3
        lda     #>_cand_box
        sta     ptr3+1
cs_loop:
        lda     cs_i
        cmp     cs_n
        jcs     cs_done
        ovl     _cs_swept, cs_next
        ; in the start box already: skip (let it escape)
        ovl     _cs_start, cs_take
        jmp     cs_next
cs_take:
        lda     _cs_axis
        asl
        tay
        lda     _cs_d+1
        jmi     cs_neg
        ; d > 0: lim = c.lo - shi; if lim < allowed
        sec
        lda     (ptr3),y
        sbc     shi
        sta     lim
        iny
        lda     (ptr3),y
        sbc     shi+1
        sta     lim+1
        sec                     ; lim - allowed < 0 ?
        lda     lim
        sbc     allowed
        lda     lim+1
        sbc     allowed+1
        bvc     :+
        eor     #$80
:       jpl     cs_next
        jmp     cs_set
cs_neg: ; d < 0: lim = c.hi - slo; if lim > allowed
        tya
        clc
        adc     #6
        tay
        sec
        lda     (ptr3),y
        sbc     slo
        sta     lim
        iny
        lda     (ptr3),y
        sbc     slo+1
        sta     lim+1
        sec                     ; allowed - lim < 0 ?
        lda     allowed
        sbc     lim
        lda     allowed+1
        sbc     lim+1
        bvc     :+
        eor     #$80
:       jpl     cs_next
cs_set: lda     lim
        sta     allowed
        lda     lim+1
        sta     allowed+1
        lda     cs_i
        asl
        tay
        lda     _cand_body,y
        sta     _cs_hit
        lda     _cand_body+1,y
        sta     _cs_hit+1
cs_next:
        inc     cs_i
        clc
        lda     ptr3
        adc     #12
        sta     ptr3
        jcc     cs_loop
        inc     ptr3+1
        jmp     cs_loop
cs_done:
        lda     allowed
        ldx     allowed+1
        rts
