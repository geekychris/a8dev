; a3d_mul.inc - the quarter-square product (a set by mulset_u/s, b in Y)
.macro  MUL
        sec
        lda     (a3d_m),y
        sbc     (a3d_m+2),y
        tax
        lda     (a3d_m+4),y
        sbc     (a3d_m+6),y
.endmacro
