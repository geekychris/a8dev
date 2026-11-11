; a3d_dirty.s - void a3d_dirty_mark(void): add lines a3d_dr[0]..a3d_dr[1]-1,
; pixels a3d_dr[2]..a3d_dr[3] (inclusive, 0..159) to the back buffer's dirty
; rectangle, for drawing done outside a3d (its own module: linked only by
; the games that call it)
        .import   dirty_add, ry0, ry1, rb0, rb1
        .export   _a3d_dirty_mark, _a3d_dr

.ifdef A3D_LOWTAB
.segment "LOWRAM"
.else
.segment "BSS"
.endif
_a3d_dr: .res 4

.segment "CODE"
_a3d_dirty_mark:
        lda     _a3d_dr
        sta     ry0
        lda     _a3d_dr+1
        sta     ry1
        lda     _a3d_dr+2
        lsr     a
        lsr     a
        sta     rb0
        lda     _a3d_dr+3
        lsr     a
        lsr     a
        sta     rb1
        jmp     dirty_add
