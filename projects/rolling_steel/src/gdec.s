; gdec.s - the course grid's decoder (tools/gen_course.py's grid_stream ->
; grid_mem), in CODE2 (RAM, loaded low: the code changes its own operands).
;
; The packed grid is at $8000 (unpacked there by course.c): nx, nz (u16),
; each row's runs (the number; per run x (u16), length), then a byte per
; cell: the height's miss from the plane through its left, upper and
; upper-left neighbours (7-bit signed), or ESC and the height (i16); then
; the surfaces, 2 bits a cell. Out at GRID: per row the number of runs;
; per run x (u16), length, base (i16, Q8); a byte a cell (height above the
; base in 1/32 unit, bits 2-7; the surface, bits 0-1); a run splits where a
; height leaves the base's -32..31 (or at 255 cells). g_row[k]: row k.
;
; Two row buffers (this row, the last) at $B000 and $B800, each four
; 512-byte arrays: the heights' low and high bytes, the row (+2) each was
; set in, low and high. Element i is reached as base + page + y (y = i's
; low byte, page its high byte, patched into the operands), element i - 1
; as base - 1 + page + y.

        .export _grid_decode
        .import _g_row, _g_nx, _g_nz
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

SRC     = $8000
GRID    = $D800
BUFA    = $B000
BUFB    = $B800
ESC     = $40

.segment "BSS"
ncell:  .res 2
kk:     .res 2                  ; the row
km:     .res 2                  ; the last row's mark (k + 1); this row's is k + 2
kn:     .res 2
nr:     .res 1
nn:     .res 1                  ; cells left in this input run
ii:     .res 2                  ; the column
first:  .res 1
hq:     .res 2
base:   .res 2
endi:   .res 2                  ; the column the open output run would take next
rn:     .res 1                  ; its cells
runs:   .res 1
rh:     .res 2                  ; its header
nrp:    .res 2
cbyte:  .res 1
ccnt:   .res 1
cl:     .res 1
cur:    .res 1
d:      .res 1

; hp = ptr1 (the run headers), rp = ptr2 (the misses), cp = ptr3 (the
; surfaces), out = ptr4

.segment "CODE2"

_grid_decode:
        lda     SRC
        sta     _g_nx
        lda     SRC+1
        sta     _g_nx+1
        lda     SRC+2
        sta     _g_nz
        lda     SRC+3
        sta     _g_nz+1
        ; the misses start after the headers: count the cells on the way
        lda     #<(SRC + 4)
        sta     ptr2
        lda     #>(SRC + 4)
        sta     ptr2+1
        lda     #0
        sta     ncell
        sta     ncell+1
        sta     kk
        sta     kk+1
@hrow:  lda     kk
        cmp     _g_nz
        lda     kk+1
        sbc     _g_nz+1
        bcs     @hdone
        ldy     #0
        lda     (ptr2),y
        tax
        jsr     inc2
@hrun:  dex
        bmi     @hnext
        ldy     #2
        lda     (ptr2),y
        clc
        adc     ncell
        sta     ncell
        bcc     @h1
        inc     ncell+1
@h1:    lda     ptr2
        clc
        adc     #3
        sta     ptr2
        bcc     @hrun
        inc     ptr2+1
        bne     @hrun
@hnext: inc     kk
        bne     @hrow
        inc     kk+1
        bne     @hrow
@hdone: ; the surfaces start after the misses
        lda     ptr2
        sta     ptr3
        lda     ptr2+1
        sta     ptr3+1
        lda     ncell
        sta     tmp1
        lda     ncell+1
        sta     tmp2
@mis:   lda     tmp1
        ora     tmp2
        beq     @mdone
        ldy     #0
        lda     (ptr3),y
        cmp     #ESC
        bne     @m1
        lda     ptr3
        clc
        adc     #2
        sta     ptr3
        bcc     @m1
        inc     ptr3+1
@m1:    inc     ptr3
        bne     @m2
        inc     ptr3+1
@m2:    lda     tmp1
        bne     @m3
        dec     tmp2
@m3:    dec     tmp1
        jmp     @mis
@mdone: ldy     #0
        lda     (ptr3),y
        sta     cbyte
        lda     #4
        sta     ccnt
        ; the buffers' marks: none yet
        lda     #<BUFA
        sta     tmp1
        lda     #>BUFA
        sta     tmp2
        lda     #0
        ldx     #16             ; 4K
        tay
@clr:   sta     (tmp1),y
        iny
        bne     @clr
        inc     tmp2
        dex
        bne     @clr
        ; go
        lda     #<(SRC + 4)
        sta     ptr1
        lda     #>(SRC + 4)
        sta     ptr1+1
        lda     #<GRID
        sta     ptr4
        lda     #>GRID
        sta     ptr4+1
        lda     #0
        sta     kk
        sta     kk+1
        sta     cur
row:    lda     kk
        cmp     _g_nz
        lda     kk+1
        sbc     _g_nz+1
        bcc     @go
        rts
@go:    ; marks: the last row's k + 1, this row's k + 2
        lda     kk
        clc
        adc     #1
        sta     km
        lda     kk+1
        adc     #0
        sta     km+1
        lda     km
        clc
        adc     #1
        sta     kn
        lda     km+1
        adc     #0
        sta     kn+1
        ; g_row[k] = out
        lda     kk
        asl
        sta     tmp1
        lda     kk+1
        rol
        sta     tmp2
        lda     tmp1
        clc
        adc     #<_g_row
        sta     tmp1
        lda     tmp2
        adc     #>_g_row
        sta     tmp2
        ldy     #0
        lda     ptr4
        sta     (tmp1),y
        iny
        lda     ptr4+1
        sta     (tmp1),y
        lda     ptr4
        sta     nrp
        lda     ptr4+1
        sta     nrp+1
        jsr     outinc
        lda     #0
        sta     runs
        sta     rh+1            ; no run open
        lda     #$FF
        sta     endi
        sta     endi+1
        ; the input runs
        ldy     #0
        lda     (ptr1),y
        sta     nr
        jsr     inc1
irun:   dec     nr
        bpl     @r
        jmp     rowend
@r:     ldy     #0
        lda     (ptr1),y
        sta     ii
        iny
        lda     (ptr1),y
        sta     ii+1
        iny
        lda     (ptr1),y
        sta     nn
        lda     ptr1
        clc
        adc     #3
        sta     ptr1
        bcc     @r1
        inc     ptr1+1
@r1:    lda     ii+1
        jsr     setpage
        lda     #1
        sta     first
cellp:  lda     nn
        bne     @c
        jmp     irun
@c:     dec     nn
        ldy     #0
        lda     (ptr2),y
        cmp     #ESC
        bne     @miss
        iny
        lda     (ptr2),y
        sta     hq
        iny
        lda     (ptr2),y
        sta     hq+1
        lda     ptr2
        clc
        adc     #3
        sta     ptr2
        bcc     @e1
        inc     ptr2+1
@e1:    jmp     store
@miss:  asl                     ; 7-bit signed -> 8
        cmp     #$80
        ror
        sta     d
        inc     ptr2
        bne     @m0
        inc     ptr2+1
@m0:    ldy     ii              ; y: the column's low byte for every array
        lda     first
        beq     gd_left
        ; the first of a run: from above
pu_l1:  lda     BUFB,y
        sta     hq
pu_h1:  lda     BUFB+$200,y
        sta     hq+1
        jmp     addd
gd_left:  ; above and above-left both set in the last row? the plane; else the left
pm_l1:  lda     BUFB+$400,y
        cmp     km
        bne     gd_lonly
pm_h1:  lda     BUFB+$600,y
        cmp     km+1
        bne     gd_lonly
pm_l2:  lda     BUFB+$400-1,y
        cmp     km
        bne     gd_lonly
pm_h2:  lda     BUFB+$600-1,y
        cmp     km+1
        bne     gd_lonly
        ; left + up - upleft
pc_l2:  lda     BUFA-1,y
        clc
pu_l2:  adc     BUFB,y
        sta     hq
pc_h2:  lda     BUFA+$200-1,y
pu_h2:  adc     BUFB+$200,y
        sta     hq+1
        lda     hq
        sec
pu_l3:  sbc     BUFB-1,y
        sta     hq
        lda     hq+1
pu_h3:  sbc     BUFB+$200-1,y
        sta     hq+1
        jmp     addd
gd_lonly:
pc_l3:  lda     BUFA-1,y
        sta     hq
pc_h3:  lda     BUFA+$200-1,y
        sta     hq+1
addd:   lda     d
        ldx     #0
        cmp     #$80
        bcc     @p
        dex
@p:     clc
        adc     hq
        sta     hq
        txa
        adc     hq+1
        sta     hq+1
store:  ldy     ii
        lda     hq
pc_l4:  sta     BUFA,y
        lda     hq+1
pc_h4:  sta     BUFA+$200,y
        lda     kn
pc_m1:  sta     BUFA+$400,y
        lda     kn+1
pc_m2:  sta     BUFA+$600,y
        lda     #0
        sta     first
        ; the surface: the next 2 bits
        lda     cbyte
        and     #3
        sta     cl
        lsr     cbyte
        lsr     cbyte
        dec     ccnt
        bne     @s
        lda     #4
        sta     ccnt
        inc     ptr3
        bne     @s1
        inc     ptr3+1
@s1:    ldy     #0
        lda     (ptr3),y
        sta     cbyte
@s:     ; the output: on in the open run, or a new one
        lda     ii
        cmp     endi
        bne     newrun
        lda     ii+1
        cmp     endi+1
        bne     newrun
        lda     rn
        cmp     #255
        beq     newrun
        lda     hq              ; d = hq - base in -32..31?
        sec
        sbc     base
        sta     tmp1
        lda     hq+1
        sbc     base+1
        beq     @pos
        cmp     #$FF
        bne     newrun
        lda     tmp1
        cmp     #$E0
        bcc     newrun
        bcs     @ok
@pos:   lda     tmp1
        cmp     #32
        bcs     newrun
@ok:    lda     tmp1
        asl
        asl
        ora     cl
        ldy     #0
        sta     (ptr4),y
        jsr     outinc
        inc     rn
        inc     endi
        bne     next
        inc     endi+1
        jmp     next
newrun: jsr     closerun
        lda     ptr4
        sta     rh
        lda     ptr4+1
        sta     rh+1
        ldy     #0
        lda     ii
        sta     (ptr4),y
        iny
        lda     ii+1
        sta     (ptr4),y
        iny                     ; (the length later)
        lda     hq
        sta     base
        sta     tmp1
        lda     hq+1
        sta     base+1
        asl     tmp1            ; Q8 = x 8
        rol
        asl     tmp1
        rol
        asl     tmp1
        rol
        ldy     #4
        sta     (ptr4),y
        dey
        lda     tmp1
        sta     (ptr4),y
        ldy     #5
        lda     cl
        sta     (ptr4),y
        lda     ptr4
        clc
        adc     #6
        sta     ptr4
        bcc     @n1
        inc     ptr4+1
@n1:    lda     #1
        sta     rn
        inc     runs
        lda     ii
        clc
        adc     #1
        sta     endi
        lda     ii+1
        adc     #0
        sta     endi+1
next:   inc     ii
        bne     @nx
        inc     ii+1
        lda     ii+1
        jsr     setpage
@nx:    jmp     cellp

rowend: jsr     closerun
        lda     nrp
        sta     tmp1
        lda     nrp+1
        sta     tmp2
        ldy     #0
        lda     runs
        sta     (tmp1),y
        ; swap the buffers
        lda     cur
        eor     #1
        sta     cur
        inc     kk
        bne     @k
        inc     kk+1
@k:     jmp     row

; the open output run's length into its header
closerun:
        lda     rh+1
        beq     @none
        sta     tmp2
        lda     rh
        sta     tmp1
        ldy     #2
        lda     rn
        sta     (tmp1),y
        lda     #0
        sta     rh+1
@none:  rts

inc1:   inc     ptr1
        bne     @r
        inc     ptr1+1
@r:     rts
inc2:   inc     ptr2
        bne     @r
        inc     ptr2+1
@r:     rts
outinc: inc     ptr4
        bne     @r
        inc     ptr4+1
@r:     rts

; the operands' pages for column page A (0 or 1) and the buffers in use:
; this row's in cur (0: BUFA, 1: BUFB), the last row's the other
setpage:
        sta     tmp4
        lda     cur
        beq     @a
        lda     #>BUFB
        ldx     #>BUFA
        bne     @b
@a:     lda     #>BUFA
        ldx     #>BUFB
@b:     clc
        adc     tmp4            ; this row's page P (element i - 1: P - 1 + y)
        sta     pc_l4+2
        sec
        sbc     #1
        sta     pc_l2+2
        sta     pc_l3+2
        clc
        adc     #3
        sta     pc_h4+2         ; P + 2
        sec
        sbc     #1
        sta     pc_h2+2         ; P + 1
        sta     pc_h3+2
        clc
        adc     #3
        sta     pc_m1+2         ; P + 4
        adc     #2
        sta     pc_m2+2         ; P + 6
        txa
        clc
        adc     tmp4            ; the last row's page Q
        sta     pu_l1+2
        sta     pu_l2+2
        sec
        sbc     #1
        sta     pu_l3+2         ; Q - 1
        clc
        adc     #3
        sta     pu_h1+2         ; Q + 2
        sta     pu_h2+2
        sec
        sbc     #1
        sta     pu_h3+2         ; Q + 1
        clc
        adc     #3
        sta     pm_l1+2         ; Q + 4
        sec
        sbc     #1
        sta     pm_l2+2         ; Q + 3
        clc
        adc     #3
        sta     pm_h1+2         ; Q + 6
        sec
        sbc     #1
        sta     pm_h2+2         ; Q + 5
        rts
