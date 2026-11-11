; ScAIry Fun - loading the code that runs under the OS ROM (scairy_fun.cfg).
; Each chunk (HIA, HIB) is a segment of the program file loaded at $A000;
; the INIT entry after it calls stage_copy, which switches the ROM off for
; a moment (interrupts off), copies the chunk to its place and switches the
; ROM back on for the loader.

        .export stage_copy
        .import __HIA_LOAD__, __HIA_RUN__, __HIA_SIZE__
        .import __HIB_LOAD__, __HIB_RUN__, __HIB_SIZE__
        .importzp ptr1, ptr2

PORTB   = $D301
NMIEN   = $D40E
INITAD  = $02E2

.segment "HIAHDR"
        .word __HIA_LOAD__, __HIA_LOAD__ + __HIA_SIZE__ - 1
.segment "HIAINIT"
        .word INITAD, INITAD + 1, stage_copy
.segment "HIBHDR"
        .word __HIB_LOAD__, __HIB_LOAD__ + __HIB_SIZE__ - 1
.segment "HIBINIT"
        .word INITAD, INITAD + 1, stage_copy

.segment "LOWDATA"
chunk:  .byte 0

.segment "LOWCODE"
dst_lo: .byte <__HIA_RUN__, <__HIB_RUN__
dst_hi: .byte >__HIA_RUN__, >__HIB_RUN__
pages:  .byte >(__HIA_SIZE__ + 255), >(__HIB_SIZE__ + 255)

.proc stage_copy
        ldx chunk
        inc chunk
        lda #<$A000
        sta ptr1
        lda #>$A000
        sta ptr1+1
        lda dst_lo,x
        sta ptr2
        lda dst_hi,x
        sta ptr2+1
        lda pages,x
        tax
        sei
        lda #0
        sta NMIEN
        lda PORTB
        and #$FE
        sta PORTB
        ldy #0
@c:     lda (ptr1),y
        sta (ptr2),y
        iny
        bne @c
        inc ptr1+1
        inc ptr2+1
        dex
        bne @c
        lda PORTB
        ora #$01
        sta PORTB
        lda #$40
        sta NMIEN
        cli
        rts
.endproc
