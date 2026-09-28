/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Position-independent ARM9 stages. Each copied region includes its literal pool. */
.syntax unified
.arm
.section .text.arm9_stages,"ax",%progbits
.balign 4
.global resetMemory2_ARM9
.type resetMemory2_ARM9,%function
resetMemory2_ARM9:
    mov r0, #0
    ldr r1, =0x040000B0
    mov r2, #12
1:  str r0, [r1], #4
    subs r2, r2, #1
    bne 1b
    ldr r1, =0x04000100
    mov r2, #4
2:  str r0, [r1], #4
    subs r2, r2, #1
    bne 2b
    ldr r1, =0x04000000
    ldr r2, =0x04001000
    mov r3, #43
3:  strh r0, [r1], #2
    strh r0, [r2], #2
    subs r3, r3, #1
    bne 3b
    ldr r1, =0x04000240
    strb r0, [r1, #0]
    strb r0, [r1, #1]
    /* Bank C belongs to the ARM7 loader. */
    strb r0, [r1, #3]
    strb r0, [r1, #4]
    strb r0, [r1, #5]
    strb r0, [r1, #6]
    strb r0, [r1, #8]
    strb r0, [r1, #9]
    mov r0, #3
    strb r0, [r1, #7]
    ldr r1, =0x04000304
    ldr r0, =0x820F
    strh r0, [r1]
    ldr r1, =0x02FFFE04
    ldr r0, =0xE59FF018
    str r0, [r1]
    str r1, [r1, #0x20]
    bx r1
    .ltorg
    .space 0x400 - (. - resetMemory2_ARM9)
.size resetMemory2_ARM9, . - resetMemory2_ARM9

.global startBinary_ARM9
.type startBinary_ARM9,%function
startBinary_ARM9:
    mov r0, #0
    ldr r1, =0x04000208
    str r0, [r1]
    ldr r1, =0x04000204
    ldr r2, =0xE880
    strh r2, [r1]
    ldr r1, =0x02FFFDFB
    strb r0, [r1]
    ldr r2, =0x04000006
4:  ldrh r3, [r2]
    cmp r3, #191
    bne 4b
5:  ldrh r3, [r2]
    cmp r3, #191
    beq 5b
6:  ldrb r3, [r1]
    cmp r3, #1
    bne 6b
    ldr r1, =0x02FFFE24
    ldr r1, [r1]
    bx r1
    .ltorg
    .space 0x100 - (. - startBinary_ARM9)
.size startBinary_ARM9, . - startBinary_ARM9

/* Only used for DSi -> NTR. No changes to the v0.10 TWL start stage.
 * Run the clock/RAM switch from ITCM so neither remapping nor a clock change
 * invalidates our instruction fetch. The header/argv were mirrored by ARM7. */
.global startBinaryNTR_ARM9
.type startBinaryNTR_ARM9,%function
startBinaryNTR_ARM9:
    mov r0, #0
    ldr r1, =0x04000208
    str r0, [r1]
    ldr r1, =0x02FFFDFB
7:  ldrb r0, [r1]
    cmp r0, #1
    bne 7b
    adr r0, ntr_itcm
    mov r1, #0x01000000
    mov r2, #0x100
8:  ldr r3, [r0], #4
    str r3, [r1], #4
    subs r2, r2, #4
    bne 8b
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mov pc, #0x01000000
    .ltorg
    .balign 4
ntr_itcm:
    ldr r1, =0x023FFDFB
    mov r0, #2
    strb r0, [r1]
11: ldrb r0, [r1]
    cmp r0, #3
    bne 11b
    ldr r1, =0x04004004
    ldrh r0, [r1]
    bic r0, r0, #1
    strh r0, [r1]
    mov r2, #8
9:  subs r2, r2, #1
    bge 9b
    mov r0, #0
    ldr r1, =0x04004060
    str r0, [r1]
    ldr r1, =0x04004040
    mov r2, #8
10: str r0, [r1], #4
    subs r2, r2, #1
    bne 10b
    ldr r1, =0x04004008
    ldr r0, =0x03000000
    str r0, [r1]             /* NTR: 4 MiB, old features, SCFG locked. */
    ldr r1, =0x04000204
    ldr r0, =0xE880
    strh r0, [r1]
    ldr r1, =0x023FFE24
    ldr r1, [r1]
    bx r1
    .ltorg
    .space 0x100 - (. - ntr_itcm)
    .space 0x200 - (. - startBinaryNTR_ARM9)
.size startBinaryNTR_ARM9, . - startBinaryNTR_ARM9
