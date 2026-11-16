/* SPDX-License-Identifier: GPL-3.0-or-later */
ENTRY(touchReadXY)
PROVIDE(cdcTouchRead = 0x0380a268);
PROVIDE(cdcTouchInit = 0x0380a128);
PROVIDE(touchPenDown = 0x03808a98);
PROVIDE(__aeabi_idiv = 0x0380e4a8);
PROVIDE(patchMain = 0x03808ae8);
PROVIDE(patchTask = 0x03809248);
SECTIONS {
    .main patchMain : {
        *(.entry) *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON)
        . = ALIGN(4);
        BYTE(0) /* Include zero-initialized state in the copied image. */
        . = ALIGN(4);
    }
    ASSERT(SIZEOF(.main) <= 0x670, "TWL main patch exceeds stock function")
    .task patchTask : { *(.task) . = ALIGN(4); }
    ASSERT(SIZEOF(.task) <= 0x200, "TWL task patch exceeds stock function")
    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
