; a8dev ATR boot loader: boots from sector 1, loads XEX files stored in
; contiguous 128-byte sectors (a8dev.atr builds the disk), no DOS needed.
;
; Disk layout (single density, 128-byte sectors):
;   sectors 1..8     this loader (boot header + code), loaded at $0700
;   sectors 9..16    directory: 32 entries of 32 bytes
;                      +0  start sector (word)   +2 length in bytes (3 bytes)
;                      +5  flags                 +6 title (26 bytes, ATASCII, 0-padded)
;                    entry 0 = the menu (the program the disk boots into)
;   sectors 17..     the files
;
; Fixed entry points for programs (the arcade menu):
;   $0706  JMP boot           reload + run the menu (entry 0)
;   $0709  JMP run_entry      A = directory entry: load that XEX and run it
;   $070C  JMP read_sector    sector in $0B/$0C (AUX), buffer in BUFRLO/HI -> carry set on error
;
; RESET returns to the menu (DOSINI/DOSVEC point at boot).

        .setcpu "6502"

DOSVEC  = $000A
DOSINI  = $000C
BOOTQ   = $0009
WARMST  = $0008
ZPTR    = $00CB         ; 2 bytes: XEX segment pointer (free zero page under cc65? we
ZEND    = $00CD         ; only run between programs, so anything goes)
ZSEC    = $00CF         ; 2 bytes: next sector
ZLEFT   = $00D1         ; 3 bytes: bytes left in the file
ZBPOS   = $00D4         ; position in the sector buffer
RUNAD   = $02E0
INITAD  = $02E2
SDMCTL  = $022F
GRACTL  = $D01D
AUDCTL  = $D208
DDEVIC  = $0300
DUNIT   = $0301
DCOMND  = $0302
DSTATS  = $0303
DBUFLO  = $0304
DBUFHI  = $0305
DTIMLO  = $0306
DBYTLO  = $0308
DBYTHI  = $0309
DAUX1   = $030A
DAUX2   = $030B
DSKINV  = $E453
COLDSV  = $E477
SECBUF  = $0B00         ; 128-byte sector buffer
DIRBUF  = $0B80         ; one directory entry (32 bytes)

NSECT   = 8
DIRSEC  = 9

        .segment "BOOT"
header: .byte   0               ; flags
        .byte   NSECT           ; sectors to load
        .word   $0700           ; load address
        .word   boot            ; DOSINI (init after boot)
        ; $0706: the OS JSRs here after loading
        jmp     first
        jmp     run_entry       ; $0709
        jmp     read_sector     ; $070C

first:  ; first entry from the OS boot: claim DOSINI/DOSVEC so RESET comes back
        lda     #<boot
        sta     DOSVEC
        sta     DOSINI
        lda     #>boot
        sta     DOSVEC+1
        sta     DOSINI+1
        lda     #1
        sta     BOOTQ           ; disk boot done: warm start calls DOSINI
        clc
        rts                     ; OS: JMP (DOSINI), then JMP (DOSVEC) -> boot

boot:   lda     #0              ; entry 0 = the menu

; A = directory entry: read it, load the XEX, run it (never returns)
run_entry:
        ldx     #$FF
        txs                     ; fresh stack (we never return)
        pha
        lda     #0
        sta     SDMCTL          ; screen off while loading
        sta     GRACTL          ; players/missiles off
        sta     AUDCTL
        ldx     #7
@snd:   sta     $D201,x         ; AUDC1..4 (odd), AUDF too: silence
        dex
        bpl     @snd
        pla
        ; directory sector = DIRSEC + entry / 4, offset (entry & 3) * 32
        pha
        lsr
        lsr
        clc
        adc     #DIRSEC
        sta     DAUX1
        lda     #0
        sta     DAUX2
        jsr     read_secbuf
        bcc     :+
        jmp     fail
:       pla
        and     #3
        asl
        asl
        asl
        asl
        asl
        tax
        ldy     #0
@cp:    lda     SECBUF,x
        sta     DIRBUF,y
        inx
        iny
        cpy     #32
        bne     @cp
        lda     DIRBUF
        sta     ZSEC
        lda     DIRBUF+1
        sta     ZSEC+1
        lda     DIRBUF+2
        sta     ZLEFT
        lda     DIRBUF+3
        sta     ZLEFT+1
        lda     DIRBUF+4
        sta     ZLEFT+2
        ora     ZLEFT+1
        ora     ZLEFT
        bne     :+
        jmp     fail            ; empty entry
:
        lda     #128
        sta     ZBPOS           ; buffer empty: read on first byte
        lda     #<rts_only
        sta     RUNAD
        lda     #>rts_only
        sta     RUNAD+1

; XEX: [$FFFF] start end data... ; INITAD set by a segment runs after it
segment:
        jsr     eof
        beq     run
        lda     #<rts_only
        sta     INITAD
        lda     #>rts_only
        sta     INITAD+1
@hdr:   jsr     getbyte
        sta     ZPTR
        jsr     getbyte
        sta     ZPTR+1
        and     ZPTR
        cmp     #$FF
        beq     @hdr            ; $FFFF marker
        jsr     getbyte
        sta     ZEND
        jsr     getbyte
        sta     ZEND+1
@data:  jsr     getbyte
        ldy     #0
        sta     (ZPTR),y
        lda     ZPTR
        cmp     ZEND
        lda     ZPTR+1
        sbc     ZEND+1
        bcs     @segdone        ; ptr >= end: that was the last byte
        inc     ZPTR
        bne     @data
        inc     ZPTR+1
        jmp     @data
@segdone:
        jsr     call_init
        jmp     segment

run:    jmp     (RUNAD)

call_init:
        jmp     (INITAD)
rts_only:
        rts

fail:   ; a red screen and a cold start on any key would be nicer; for now cold start
        lda     #$34
        sta     $02C8
        jmp     COLDSV

; Z = 1 when no bytes are left
eof:    lda     ZLEFT
        ora     ZLEFT+1
        ora     ZLEFT+2
        rts

; next byte of the file (A), reading sectors as needed
getbyte:
        ldx     ZBPOS
        bpl     @have
        lda     ZSEC
        sta     DAUX1
        lda     ZSEC+1
        sta     DAUX2
        jsr     read_secbuf
        bcc     :+
        jmp     fail
:       inc     ZSEC
        bne     :+
        inc     ZSEC+1
:       ldx     #0
@have:  lda     SECBUF,x
        inx
        stx     ZBPOS
        pha
        lda     ZLEFT           ; 24-bit decrement
        bne     @l0
        lda     ZLEFT+1
        bne     @l1
        dec     ZLEFT+2
@l1:    dec     ZLEFT+1
@l0:    dec     ZLEFT
        pla
        rts

read_secbuf:
        lda     #<SECBUF
        sta     DBUFLO
        lda     #>SECBUF
        sta     DBUFHI
; read sector DAUX1/2 into DBUFLO/HI; carry set on error
read_sector:
        lda     #$31
        sta     DDEVIC
        lda     #1
        sta     DUNIT
        lda     #$52            ; 'R'
        sta     DCOMND
        lda     #$40
        sta     DSTATS          ; read
        lda     #128
        sta     DBYTLO
        lda     #0
        sta     DBYTHI
        lda     #7
        sta     DTIMLO
        jsr     DSKINV
        lda     DSTATS
        cmp     #1
        beq     @ok
        sec
        rts
@ok:    clc
        rts

        .res    NSECT * 128 - (* - header), 0
