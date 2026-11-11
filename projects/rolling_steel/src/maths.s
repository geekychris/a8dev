; maths.s - the physics' hot helpers: a grid cell's height, and a point's
; line in the course picture.

        .export _cell_q, _cq_i, _cq_k, _cell_h, _cell_c, _pic_line
        .export _ground_q, _gq_x, _gq_z, _g_h, _g_sx, _g_sz, _g_cls
        .import _g_row, _g_nz, popax
_meta   = $0400
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

.segment "ZEROPAGE"
_cq_i:  .res 2
_cq_k:  .res 2
_gq_x:  .res 2
_gq_z:  .res 2
tx:     .res 1
tz:     .res 1
.segment "BSS"
_g_h:   .res 2
_g_sx:  .res 2
_g_sz:  .res 2
_g_cls: .res 1
ci0:    .res 2
ck0:    .res 2
jj:     .res 1
ok:     .res 4
cl:     .res 4
hh:     .res 8
hc:     .res 2
dx0:    .res 2
dx1:    .res 2
dz0:    .res 2
dz1:    .res 2
aa:     .res 2
bb:     .res 2
_cell_h: .res 2
_cell_c: .res 1
r0:     .res 1                  ; mul: the 24-bit product
r1:     .res 1
r2:     .res 1
k0:     .res 1                  ; K = gy 210 + gz 147 (24-bit, signed)
k1:     .res 1
k2:     .res 1
neg:    .res 1

.segment "RODATA"
di:     .byte 0, 1, 0, 1
dk:     .byte 0, 0, 1, 1

.segment "CODE"

; uint8_t cell_q(void): is the cell (cq_i, cq_k) deck? 1: cell_h its
; height (Q8), cell_c its surface; 0: void. A row is its number of runs,
; then runs of: x (u16), length, base (i16, Q8), a byte a cell (height
; above the base in 1/32 unit, bits 2-7, signed; the surface, bits 0-1).
_cell_q:
        lda     _cq_k
        cmp     _g_nz
        lda     _cq_k+1
        sbc     _g_nz+1
        bcs     @none           ; k >= nz
        lda     _cq_k           ; ptr1 = g_row[k]
        asl
        tay
        lda     _cq_k+1
        rol
        tax
        tya
        clc
        adc     #<_g_row
        sta     ptr2
        txa
        adc     #>_g_row
        sta     ptr2+1
        ldy     #0
        lda     (ptr2),y
        sta     ptr1
        iny
        lda     (ptr2),y
        sta     ptr1+1
        dey
        lda     (ptr1),y        ; the runs
        beq     @none
        tax
        inc     ptr1
        bne     @run
        inc     ptr1+1
@run:   ; d = i - x
        ldy     #0
        lda     _cq_i
        sec
        sbc     (ptr1),y
        sta     tmp3
        iny
        lda     _cq_i+1
        sbc     (ptr1),y
        bcc     @none           ; i < x: the runs only go right from here
        bne     @next           ; d >= 256: past this run
        iny
        lda     tmp3
        cmp     (ptr1),y        ; d < length?
        bcc     @hit
@next:  ldy     #2              ; ptr1 += 5 + length
        lda     (ptr1),y
        clc
        adc     #5
        adc     ptr1
        sta     ptr1
        bcc     @n2
        inc     ptr1+1
@n2:    dex
        bne     @run
@none:  lda     #0
        tax
        rts
@hit:   lda     tmp3
        clc
        adc     #5
        tay
        lda     (ptr1),y        ; the cell
        tax
        and     #3
        sta     _cell_c
        txa
        and     #$FC            ; h << 2 (signed) -> h * 8 = (h << 2) * 2
        ldx     #0
        asl
        bcc     @pos
        dex                     ; negative: the high byte $FF
@pos:   ldy     #3
        clc
        adc     (ptr1),y
        sta     _cell_h
        txa
        iny
        adc     (ptr1),y
        sta     _cell_h+1
        lda     #1
        ldx     #0
        rts

; uint8_t ground_q(void): the deck under (gq_x, gq_z): 1 and g_h (Q8,
; bilinear between the four cell centres around the point; a neighbour
; that is void or more than STEPUP off the point's own cell stands in at
; its height), g_sx, g_sz (the rise across one cell, Q8), g_cls; 0: void
STEPUP  = 64
_ground_q:
        lda     _gq_x           ; fx = gx - 64: i = fx >> 7, tx = fx & 127
        sec
        sbc     #64
        sta     tmp1
        lda     _gq_x+1
        sbc     #0
        sta     tmp2
        lda     tmp1
        and     #127
        sta     tx
        asl     tmp1            ; i = (tmp2:tmp1) >> 7
        lda     tmp2
        rol
        sta     ci0
        lda     #0
        rol
        sta     ci0+1
        lda     _gq_z
        sec
        sbc     #64
        sta     tmp1
        lda     _gq_z+1
        sbc     #0
        sta     tmp2
        lda     tmp1
        and     #127
        sta     tz
        asl     tmp1
        lda     tmp2
        rol
        sta     ck0
        lda     #0
        rol
        sta     ck0+1
        ; the four cells: j = 0 (i, k), 1 (i+1, k), 2 (i, k+1), 3 (i+1, k+1)
        ldx     #0
@four:  stx     jj
        lda     ci0
        clc
        adc     di,x
        sta     _cq_i
        lda     ci0+1
        adc     #0
        sta     _cq_i+1
        lda     ck0
        clc
        adc     dk,x
        sta     _cq_k
        lda     ck0+1
        adc     #0
        sta     _cq_k+1
        jsr     _cell_q
        ldx     jj
        sta     ok,x
        lda     _cell_c
        sta     cl,x
        txa
        asl
        tay
        lda     _cell_h
        sta     hh,y
        lda     _cell_h+1
        sta     hh+1,y
        inx
        cpx     #4
        bne     @four
        ; the point's own cell
        ldx     #0
        lda     tx
        cmp     #64
        bcc     @a
        inx
@a:     lda     tz
        cmp     #64
        bcc     @b
        inx
        inx
@b:     lda     ok,x
        bne     @have
        lda     #0
        tax
        rts
@have:  lda     cl,x
        sta     _g_cls
        txa
        asl
        tay
        lda     hh,y
        sta     hc
        lda     hh+1,y
        sta     hc+1
        ; the others: void or a wall -> the cell's own height
        ldx     #3
@fix:   txa
        asl
        tay
        lda     ok,x
        beq     @use
        lda     hh,y            ; d = h - hc, within +-STEPUP?
        sec
        sbc     hc
        sta     tmp1
        lda     hh+1,y
        sbc     hc+1
        ; -STEPUP <= d <= STEPUP: hi 0 and lo <= STEPUP, or hi $FF and lo >= -STEPUP
        beq     @pos0
        cmp     #$FF
        bne     @use
        lda     tmp1
        cmp     #<(-STEPUP)
        bcs     @keep
        bcc     @use
@pos0:  lda     tmp1
        cmp     #STEPUP + 1
        bcc     @keep
@use:   lda     hc
        sta     hh,y
        lda     hc+1
        sta     hh+1,y
@keep:  dex
        bpl     @fix
        ; dx0 = h1 - h0, dx1 = h3 - h2, dz0 = h2 - h0, dz1 = h3 - h1 (each within +-128: 9 bits)
        lda     hh+2
        sec
        sbc     hh+0
        sta     dx0
        lda     hh+3
        sbc     hh+1
        sta     dx0+1
        lda     hh+6
        sec
        sbc     hh+4
        sta     dx1
        lda     hh+7
        sbc     hh+5
        sta     dx1+1
        lda     hh+4
        sec
        sbc     hh+0
        sta     dz0
        lda     hh+5
        sbc     hh+1
        sta     dz0+1
        lda     hh+6
        sec
        sbc     hh+2
        sta     dz1
        lda     hh+7
        sbc     hh+3
        sta     dz1+1
        ; a = h0 + dx0 tx / 128; b = h2 + dx1 tx / 128
        lda     dx0
        ldx     dx0+1
        ldy     tx
        jsr     smul7
        clc
        adc     hh+0
        sta     aa
        txa
        adc     hh+1
        sta     aa+1
        lda     dx1
        ldx     dx1+1
        ldy     tx
        jsr     smul7
        clc
        adc     hh+4
        sta     bb
        txa
        adc     hh+5
        sta     bb+1
        ; g_h = a + (b - a) tz / 128 (b - a within +-128)
        lda     bb
        sec
        sbc     aa
        pha
        lda     bb+1
        sbc     aa+1
        tax
        pla
        ldy     tz
        jsr     smul7
        clc
        adc     aa
        sta     _g_h
        txa
        adc     aa+1
        sta     _g_h+1
        ; g_sx = dx0 + (dx1 - dx0) tz / 128
        lda     dx1
        sec
        sbc     dx0
        pha
        lda     dx1+1
        sbc     dx0+1
        tax
        pla
        ldy     tz
        jsr     smul7
        clc
        adc     dx0
        sta     _g_sx
        txa
        adc     dx0+1
        sta     _g_sx+1
        ; g_sz = dz0 + (dz1 - dz0) tx / 128
        lda     dz1
        sec
        sbc     dz0
        pha
        lda     dz1+1
        sbc     dz0+1
        tax
        pla
        ldy     tx
        jsr     smul7
        clc
        adc     dz0
        sta     _g_sz
        txa
        adc     dz0+1
        sta     _g_sz+1
        lda     #1
        ldx     #0
        rts

; A/X (signed, within +-255) * Y (0..127) / 128 -> A/X (signed, toward -inf)
smul7:  sty     tmp4
        stx     neg
        cpx     #$80
        bcc     @p
        eor     #$FF            ; |v| (v >= -255)
        clc
        adc     #1
@p:     sta     tmp3            ; |v| low 8 bits (|v| <= 255)
        ; r1:r0 = tmp3 * tmp4 (8 x 8)
        lda     #0
        ldx     #8
        lsr     tmp4
@m:     bcc     @n
        clc
        adc     tmp3
@n:     ror
        ror     tmp4
        dex
        bne     @m
        ; product = A:tmp4; / 128 = (A:tmp4) << 1 >> 8
        asl     tmp4
        rol
        ldx     #0
        bit     neg
        bpl     @r
        eor     #$FF            ; negate (floor toward -inf: -(q+1) if remainder, close enough)
        clc
        adc     #1
        ldx     #$FF
        cmp     #0
        bne     @r
        ldx     #0
@r:     rts

; r0..r2 = ptr1 (u16) * tmp1 (u8)
umul:   lda     #0
        sta     r0
        sta     r1
        sta     r2
        ldx     #8
@l:     asl     r0
        rol     r1
        rol     r2
        asl     tmp1
        bcc     @n
        lda     r0
        clc
        adc     ptr1
        sta     r0
        lda     r1
        adc     ptr1+1
        sta     r1
        bcc     @n
        inc     r2
@n:     dex
        bne     @l
        rts

; int16_t __fastcall__ pic_line(int16_t gy, uint16_t gz): the picture line
; of a point: ly0 - ((gy 210 + gz 147) >> 13) (cos and sin of the
; original's 35-degree pitch; tools/gen_course.py's meta())
_pic_line:
        sta     ptr1
        stx     ptr1+1
        lda     #147
        sta     tmp1
        jsr     umul
        lda     r0
        sta     k0
        lda     r1
        sta     k1
        lda     r2
        sta     k2
        jsr     popax           ; gy
        ldy     #0
        sty     neg
        cpx     #$80
        bcc     @p
        dec     neg             ; |gy|
        eor     #$FF
        clc
        adc     #1
        tay
        txa
        eor     #$FF
        adc     #0
        tax
        tya
@p:     sta     ptr1
        stx     ptr1+1
        lda     #210
        sta     tmp1
        jsr     umul
        bit     neg
        bmi     @sub
        lda     k0
        clc
        adc     r0
        sta     k0
        lda     k1
        adc     r1
        sta     k1
        lda     k2
        adc     r2
        sta     k2
        jmp     @shift
@sub:   lda     k0
        sec
        sbc     r0
        sta     k0
        lda     k1
        sbc     r1
        sta     k1
        lda     k2
        sbc     r2
        sta     k2
@shift: ; (k2:k1) >> 5, arithmetic: K >> 13
        ldx     #5
@s:     lda     k2
        cmp     #$80
        ror     k2
        ror     k1
        dex
        bne     @s
        ; ly0 - that
        lda     _meta+2         ; MetaHdr.ly0
        sec
        sbc     k1
        tay
        lda     _meta+3
        sbc     k2
        tax
        tya
        rts

; void __fastcall__ dec5(uint16_t v): v as five screen-code digits in
; decbuf, leading zeros blank (the last digit always shown)
        .export _dec5, _decbuf
.segment "BSS"
_decbuf: .res 5
dv:     .res 2
dt:     .res 1
.segment "RODATA"
p10lo:  .byte <10000, <1000, <100, <10
p10hi:  .byte >10000, >1000, >100, >10
.segment "CODE"
_dec5:  sta     dv
        stx     dv+1
        ldy     #0
@dig:   ldx     #0
@sub:   lda     dv
        sec
        sbc     p10lo,y
        sta     dt
        lda     dv+1
        sbc     p10hi,y
        bcc     @done
        sta     dv+1
        lda     dt
        sta     dv
        inx
        bne     @sub
@done:  txa
        ora     #$10
        sta     _decbuf,y
        iny
        cpy     #4
        bne     @dig
        lda     dv
        ora     #$10
        sta     _decbuf+4
        ldy     #0
@bl:    lda     _decbuf,y
        cmp     #$10
        bne     @end
        lda     #0
        sta     _decbuf,y
        iny
        cpy     #4
        bne     @bl
@end:   rts

; uint8_t __fastcall__ hud_time(uint8_t x, uint16_t t): t (steps, 1/50 s)
; as seconds and tenths, "SSS.T", at hud[x]; returns x + 5
        .export _hud_time
        .import _hud, popa
.segment "BSS"
hx:     .res 1
.segment "CODE"
_hud_time:
        sta     dv              ; dv = t / 5 (shift and subtract)
        stx     dv+1
        jsr     popa
        sta     hx
        lda     #0
        sta     dt
        ldx     #16
@d:     asl     dv
        rol     dv+1
        rol     dt
        lda     dt
        sec
        sbc     #5
        bcc     @n
        sta     dt
        inc     dv
@n:     dex
        bne     @d
        lda     dv
        ldx     dv+1
        jsr     _dec5
        lda     _decbuf+3       ; the units of seconds: never blank
        bne     @u
        lda     #$10
        sta     _decbuf+3
@u:     ldx     hx
        lda     _decbuf+1
        sta     _hud,x
        lda     _decbuf+2
        sta     _hud+1,x
        lda     _decbuf+3
        sta     _hud+2,x
        lda     #$0E            ; "."
        sta     _hud+3,x
        lda     _decbuf+4
        sta     _hud+4,x
        txa
        clc
        adc     #5
        ldx     #0
        rts

; uint8_t __fastcall__ acid_at(uint16_t x, int16_t y, uint16_t z): is the
; point in an acid pool (a BOX_ACID trigger box: y within y0..y1, z's
; grid row within its rows, x within the row's x0..x1)? (main.c's autopilot)
        .export _acid_at
        .import _boxes, popax
.segment "BSS"
az:     .res 2
ay:     .res 2
ax:     .res 2
ak:     .res 2
anb:    .res 1
.segment "CODE"
_acid_at:
        sta     az              ; k = z >> 7
        stx     az+1
        asl
        txa
        rol
        sta     ak
        lda     #0
        rol
        sta     ak+1
        jsr     popax
        sta     ay
        stx     ay+1
        jsr     popax
        sta     ax
        stx     ax+1
        lda     _boxes
        sta     ptr1
        lda     _boxes+1
        sta     ptr1+1
        lda     _meta+21        ; MetaHdr.nbox
        sta     anb
@box:   lda     anb
        bne     @b1
        lda     #0
        tax
        rts
@b1:    dec     anb
        ldy     #0              ; kind: BOX_ACID = 0
        lda     (ptr1),y
        bne     @next
        ldy     #4              ; y >= y0 (signed)
        lda     ay
        cmp     (ptr1),y
        iny
        lda     ay+1
        sbc     (ptr1),y
        bvc     @v1
        eor     #$80
@v1:    bmi     @next
        ldy     #6              ; y <= y1: y1 - y >= 0
        lda     (ptr1),y
        cmp     ay
        iny
        lda     (ptr1),y
        sbc     ay+1
        bvc     @v2
        eor     #$80
@v2:    bmi     @next
        ldy     #2              ; r = k - k0 (unsigned), r < nrows
        lda     ak
        sec
        sbc     (ptr1),y
        sta     tmp1
        iny
        lda     ak+1
        sbc     (ptr1),y
        bcc     @next           ; k < k0
        bne     @next
        ldy     #1
        lda     tmp1
        cmp     (ptr1),y
        bcs     @next
        ; the row: ptr1 + 10 + 4 r: x0 <= x <= x1
        asl
        asl                     ; (nrows < 64)
        clc
        adc     #10
        tay
        lda     ax
        cmp     (ptr1),y
        iny
        lda     ax+1
        sbc     (ptr1),y
        bcc     @next
        iny
        lda     (ptr1),y        ; x1 - x >= 0
        cmp     ax
        iny
        lda     (ptr1),y
        sbc     ax+1
        bcc     @next
        lda     #1
        ldx     #0
        rts
@next:  ldy     #1              ; ptr1 += 10 + 4 nrows
        lda     (ptr1),y
        asl
        asl
        clc
        adc     #10
        bcc     @n1
        inc     ptr1+1
        clc
@n1:    adc     ptr1
        sta     ptr1
        bcc     @nb
        inc     ptr1+1
@nb:    jmp     @box

; uint16_t __fastcall__ speed_of(int16_t vx, int16_t vz): |v| from above,
; the octagon: max + min / 4 + min / 8 (phys.c's, in assembly)
        .export _speed_of
.segment "BSS"
so_a:   .res 2
so_b:   .res 2
.segment "CODE"
_speed_of:
        jsr     soabs           ; vz -> so_b
        sta     so_b
        stx     so_b+1
        jsr     popax
        jsr     soabs
        cmp     so_b            ; |vx| < |vz|: swap
        pha
        txa
        sbc     so_b+1
        bcs     @ok
        pla                     ; a = so_b, b = |vx|
        ldy     so_b
        sta     so_b
        tya
        pha
        lda     so_b+1
        stx     so_b+1
        tax
@ok:    pla
        sta     so_a
        stx     so_a+1
        lda     so_b+1          ; b >> 2
        lsr
        sta     tmp2
        lda     so_b
        ror
        lsr     tmp2
        ror
        sta     tmp1
        lda     tmp2            ; (b >> 2) >> 1 = b >> 3
        lsr
        sta     tmp4
        lda     tmp1
        ror
        clc
        adc     tmp1
        sta     tmp3
        lda     tmp4
        adc     tmp2
        tax                     ; X:tmp3 = b/4 + b/8
        lda     tmp3
        clc
        adc     so_a
        pha
        txa
        adc     so_a+1
        tax
        pla
        rts
soabs:  cpx     #$80
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
