/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include "tgds_io.h"
#include "twl_input_generated.h"

static uint32_t crc32(const unsigned char *p, size_t n)
{
    uint32_t crc = UINT32_MAX;
    while (n--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void write32(unsigned char *p, uint32_t word)
{
    for (unsigned byte = 0; byte < 4; ++byte) p[byte] = word >> (byte * 8);
}

/* Position-independent ARM code. See tests/dspico_io_reference.s.
 * Executes the original PICO DLDI on ARM9 with the original caller buffer;
 * saves/restores IME and EXMEMCNT and propagates the driver's result.
 * v0.6 masks IRQs only around Slot-1 ownership changes. The pinned emulator's
 * ARM9 display handlers do not perform storage I/O or change card ownership;
 * DSpico polling uses no ARM7-only services. Do not use this for arbitrary apps.
 */
static const uint32_t direct_io[] = {
    0xE92D41F0, 0xE59F403C, 0xE59F603C, 0xE5967000,
    0xE3A08000, 0xE5868000, 0xE1D450B0, 0xE3C58B02,
    0xE1C480B0, 0xE5867000, 0xE59F8020, 0xE5988070,
    0xE12FFF38, 0xE3A08000, 0xE5868000, 0xE1C450B0,
    0xE5867000, 0xE8BD81F0, 0x04000204, 0x04000208,
    0x02001800
};

bool shim_tgds_dspico_io(void *arm9, size_t size, bool apply)
{
    unsigned char *p = arm9;
    const size_t offsets[2] = {0x518CC, 0x51A60};
    if (!p || size != 0x5BCCC ||
        crc32(p + offsets[0], 0x194) != 0xDAB01983 ||
        crc32(p + offsets[1], 0x194) != 0xA9BE5C37 ||
        crc32(p + 0x232A8, 0x4C) != 0x0DAD4FE0 ||
        p[0x1840] != 0 || p[0x1841] != 0x18 ||
        p[0x1842] != 0 || p[0x1843] != 2)
        return false;
    if (!apply) return true;
    for (unsigned fn = 0; fn < 2; ++fn) {
        for (unsigned i = 0; i < sizeof(direct_io) / sizeof(direct_io[0]); ++i) {
            uint32_t word = direct_io[i];
            if (fn && i == 11) word += 4; /* writeSectors at DLDI + 0x74 */
            write32(p + offsets[fn] + i * 4, word);
        }
    }
    /* FS_init failure: report 256 + FRESULT instead of the generic stage 1.
     * r4 holds f_mount's result. Success branches around this instruction.
     * add r0,r4,#256, replacing mov r0,#1. No error is bypassed.
     */
    write32(p + 0x232D0, 0xE2840C01);
    return true;
}

bool shim_tgds_twl_touch(void *arm7, size_t size, bool apply)
{
    unsigned char *p = arm7;
    /* Check each complete affected routine, including literal pools, before
     * changing any byte. ARM7 relocates this image to 0x03800000 itself.
     */
    if (!p || size != 0xF784 ||
        crc32(p + 0x8AAC, 0x670) != 0x2EE96C2C || /* touchReadXY */
        crc32(p + 0x920C, 0x200) != 0x58AA18F0 || /* taskARM7TouchScreen */
        crc32(p + 0x6C70, 0x14) != 0x03A9AAAF || /* disabled exception setup */
        crc32(p + 0x914C, 0x60) != 0x98769A97 || /* TWLSetTouchscreenTWLMode */
        crc32(p + 0xA0EC, 0xDC) != 0xDEB3FD7E || /* cdcTouchInit tail target */
        crc32(p + 0xDC28, 0x84) != 0x02A917B0 || /* VblankUser */
        crc32(p + 0xDCB0, 0x5C) != 0xE51A8EB8)   /* VcounterUser */
        return false;
    if (!apply) return true;
    for (unsigned i = 0; i < sizeof(twl_main)/4; ++i)
        write32(p + 0x8AAC + i*4, twl_main[i]);
    for (unsigned i = 0; i < sizeof(twl_task)/4; ++i)
        write32(p + 0x920C + i*4, twl_task[i]);
    /* Explicitly start 12-bit ADC conversions, enable pen detection, and use
     * the SPI data buffer with automatic updates. Do not preserve inherited
     * STOP / disable / hold bits that can leave the scanner inert. */
    write32(p + 0xA104, 0xE3A020F8); /* mov r2,#0xf8 (mask) */
    write32(p + 0xA10C, 0xE3A03018); /* mov r3,#0x18 (value) */
    write32(p + 0xA17C, 0xE3A02087); /* pen disable + sense mask */
    write32(p + 0xA1A4, 0xE3A020E7); /* buffer/hold/debounce mask */
    /* Initialize the scanner before the first pen-down test, rather than
     * waiting for a detected press to initialize the hardware that detects it.
     * The original routine has already restored its stack: tail-call init.
     */
    write32(p + 0x919C, TWL_MODE_BRANCH); /* preserve mode setup, then codec init */
    /* The TGDS IRQ has just sampled the TWL codec. Preserve that result:
     * skip the later EXTKEYIN fallback, which overwrites its pen bit.
     * Leave the existing lid/backlight/sound sleep handling intact.
     */
    write32(p + 0xDCB4, 0xEA000007); /* b 0x0380DCD8 */
    /* Keep codec sampling active past the old 600-frame timeout; otherwise
     * the removed DS-only fallback cannot wake a disabled TWL touchscreen.
     */
    write32(p + 0xDC8C, 0xE1A00000); /* nop instead of disableARM7TouchScreen */
    return true;
}
