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

/* Position-independent ARM code; see tests/dspico_io_reference.s on the
 * tests branch.
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
    static const struct {
        size_t size, read, fs;
        uint32_t read_crc, write_crc, fs_crc;
    } builds[] = {
        {0x5BCCC, 0x518CC, 0x232A8, 0xDAB01983, 0xA9BE5C37, 0x0DAD4FE0},
        {0x6608C, 0x5BBBC, 0x240E0, 0x0356C755, 0xCD2F668E, 0x9192D29B},
        {0x57628, 0x4D038, 0x1FE2C, 0xF90A7883, 0x3C306CC2, 0xAEEDC9A5},
    };
    unsigned build;
    for (build = 0; build < sizeof(builds) / sizeof(builds[0]); ++build)
        if (size == builds[build].size) break;
    if (!p || build == sizeof(builds) / sizeof(builds[0])) return false;
    const size_t offsets[2] = {builds[build].read, builds[build].read + 0x194};
    size_t fs = builds[build].fs;
    if (crc32(p + offsets[0], 0x194) != builds[build].read_crc ||
        crc32(p + offsets[1], 0x194) != builds[build].write_crc ||
        crc32(p + fs, 0x4C) != builds[build].fs_crc ||
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
    /* Keep the stable build's existing diagnostic; alternate builds only
     * need the sector routing changes above. */
    if (size == 0x5BCCC) write32(p + fs + 0x28, 0xE2840C01);
    return true;
}

bool shim_tgds_twl_touch(void *arm7, size_t size, bool apply)
{
    unsigned char *p = arm7;
    static const struct {
        size_t size, delta, vblank, vcounter, vcounter_size;
        uint32_t main_crc, task_crc, mode_crc, vblank_crc, vcounter_crc, branch;
        const uint32_t *main, *task;
        size_t main_size, task_size;
    } builds[] = {
        {0xF784, 0, 0xDC28, 0xDCB0, 0x5C,
         0x2EE96C2C, 0x58AA18F0, 0x98769A97, 0x02A917B0, 0xE51A8EB8, TWL_MODE_BRANCH,
         twl_main, twl_task, sizeof(twl_main), sizeof(twl_task)},
        {0xF6C0, 0x80, 0xDBE8, 0xDC70, 0x78,
         0xB5F69E50, 0x60083B9D, 0x853E61EB, 0x015D96B8, 0x8E5D01F3, DSP_MODE_BRANCH,
         dsp_main, dsp_task, sizeof(dsp_main), sizeof(dsp_task)},
        {0xF4F4, 0x120, 0xDAAC, 0xDB34, 0x78,
         0xE894443C, 0xFF911F12, 0x17A5EB8F, 0x428BAA4D, 0xCB8B504D, SDD1_MODE_BRANCH,
         sdd1_main, sdd1_task, sizeof(sdd1_main), sizeof(sdd1_task)},
    };
    unsigned build;
    for (build = 0; build < sizeof(builds) / sizeof(builds[0]); ++build)
        if (size == builds[build].size) break;
    if (!p || build == sizeof(builds) / sizeof(builds[0])) return false;
    size_t delta = builds[build].delta;
    /* Check each complete affected routine, including literal pools, before
     * changing any byte. ARM7 relocates this image to 0x03800000 itself.
     */
    if (crc32(p + 0x8AAC - delta, 0x670) != builds[build].main_crc ||
        crc32(p + 0x920C - delta, 0x200) != builds[build].task_crc ||
        crc32(p + 0x6C70 - delta, 0x14) != 0x03A9AAAF ||
        crc32(p + 0x914C - delta, 0x60) != builds[build].mode_crc ||
        crc32(p + 0xA0EC - delta, 0xDC) != 0xDEB3FD7E ||
        crc32(p + builds[build].vblank, 0x84) != builds[build].vblank_crc ||
        crc32(p + builds[build].vcounter, builds[build].vcounter_size) != builds[build].vcounter_crc)
        return false;
    if (!apply) return true;
    for (unsigned i = 0; i < builds[build].main_size/4; ++i)
        write32(p + 0x8AAC - delta + i*4, builds[build].main[i]);
    for (unsigned i = 0; i < builds[build].task_size/4; ++i)
        write32(p + 0x920C - delta + i*4, builds[build].task[i]);
    /* Explicitly start 12-bit ADC conversions, enable pen detection, and use
     * the SPI data buffer with automatic updates. Do not preserve inherited
     * STOP / disable / hold bits that can leave the scanner inert. */
    write32(p + 0xA104 - delta, 0xE3A020F8); /* mov r2,#0xf8 (mask) */
    write32(p + 0xA10C - delta, 0xE3A03018); /* mov r3,#0x18 (value) */
    write32(p + 0xA17C - delta, 0xE3A02087); /* pen disable + sense mask */
    write32(p + 0xA1A4 - delta, 0xE3A020E7); /* buffer/hold/debounce mask */
    /* Initialize the scanner before the first pen-down test, rather than
     * waiting for a detected press to initialize the hardware that detects it.
     * The original routine has already restored its stack: tail-call init.
     */
    write32(p + 0x919C - delta, builds[build].branch); /* mode setup, then codec init */
    /* The TGDS IRQ has just sampled the TWL codec. Preserve that result:
     * skip the later EXTKEYIN fallback, which overwrites its pen bit.
     * Leave the existing lid/backlight/sound sleep handling intact.
     */
    write32(p + builds[build].vcounter + 4, 0xEA000007); /* skip EXTKEYIN fallback */
    /* Keep codec sampling active past the old 600-frame timeout; otherwise
     * the removed DS-only fallback cannot wake a disabled TWL touchscreen.
     */
    write32(p + builds[build].vblank + 0x64, 0xE1A00000); /* skip disableARM7TouchScreen */
    return true;
}
