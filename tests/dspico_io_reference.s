/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Reference assembly for the position-independent ARM9 patch in tgds_io.c.
 * r0=sector, r1=count, r2=buffer. Caller buffer is accessed directly on ARM9.
 * Build twice with IO_OFFSET=0x70 (read) and 0x74 (write).
 */
.syntax unified
.arm
.global dspico_io
dspico_io:
    push {r4,r5,r6,r7,r8,lr}
    ldr r4, exmemcnt
    ldr r6, ime
    ldr r7, [r6]
    mov r8, #0
    str r8, [r6]
    ldrh r5, [r4]
    bic r8, r5, #0x800
    strh r8, [r4]
    str r7, [r6]            /* Let the caller's IRQs run during storage I/O. */
    ldr r8, donor
    ldr r8, [r8, #IO_OFFSET]
    blx r8
    mov r8, #0
    str r8, [r6]            /* Only the ownership changes need exclusion. */
    strh r5, [r4]
    str r7, [r6]
    pop {r4,r5,r6,r7,r8,pc}
exmemcnt: .word 0x04000204
ime:      .word 0x04000208
donor:    .word 0x02001800
