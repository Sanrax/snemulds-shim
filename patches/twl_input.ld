/* SPDX-License-Identifier: GPL-3.0-or-later */
ENTRY(touchReadXY)
cdcTouchRead = 0x0380a22c;
cdcTouchInit = 0x0380a0ec;
touchPenDown = 0x03808a5c;
__aeabi_idiv = 0x0380e414;
SECTIONS {
    .main 0x03808aac : {
        *(.entry) *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON)
        . = ALIGN(4);
        BYTE(0) /* Include zero-initialized state in the copied image. */
        . = ALIGN(4);
    }
    ASSERT(SIZEOF(.main) <= 0x670, "TWL main patch exceeds stock function")
    .task 0x0380920c : { *(.task) . = ALIGN(4); }
    ASSERT(SIZEOF(.task) <= 0x200, "TWL task patch exceeds stock function")
    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
