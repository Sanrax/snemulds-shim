/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/*
 * TGDS 1.65's ARM7InitDLDI() selects internal SD when it is available.
 * For a TWL application loaded from a DSi-mode flashcart, replace only the
 * branch that enters that decision with an unconditional branch to TGDS's
 * existing DLDI initialization path. The surrounding O0 instruction sequence
 * makes this deliberately narrower than a version- or address-only patch.
 */
size_t shim_tgds_dldi_patch_offset(const void *data, size_t size)
{
    const unsigned char *p = data;
    size_t found = SIZE_MAX;
    unsigned matches = 0;
    if (!p) return SIZE_MAX;
    for (size_t i = 0; i + 36 <= size; i += 4) {
        if (read32(p + i) != 0xE5933000 ||       /* ldr r3, [r3] */
            read32(p + i + 4) != 0xE3530003 ||   /* cmp r3, #3 */
            read32(p + i + 8) != 0x0A00000B ||   /* beq DLDI path */
            (read32(p + i + 12) & 0xFF000000) != 0xEB000000 ||
            read32(p + i + 16) != 0xE1A03000 ||
            read32(p + i + 20) != 0xE2233001 ||
            read32(p + i + 24) != 0xE20330FF ||
            read32(p + i + 28) != 0xE3530000 ||
            read32(p + i + 32) != 0x1A000005)
            continue;
        found = i + 8;
        ++matches;
    }
    return matches == 1 ? found : SIZE_MAX;
}
