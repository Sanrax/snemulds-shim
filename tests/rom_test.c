/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void header(FILE *file, long offset, unsigned map, unsigned chip, unsigned maker)
{
    unsigned char h[64] = {0};
    memcpy(h, "RENAMED TEST GAME     ", 21);
    h[0x15] = map; h[0x16] = chip; h[0x17] = 10; h[0x1A] = maker;
    h[0x1C] = 0xCB; h[0x1D] = 0xED; h[0x1E] = 0x34; h[0x1F] = 0x12;
    h[0x3D] = 0x80;
    assert(!fseek(file, offset, SEEK_SET));
    assert(fwrite(h, 1, sizeof(h), file) == sizeof(h));
}

static void rom(const char *path, long size, long prefix, long offset,
                 unsigned map, unsigned chip, unsigned maker)
{
    FILE *file = fopen(path, "wb"); assert(file);
    assert(!fseek(file, size + prefix - 1, SEEK_SET)); assert(fputc(0, file) == 0);
    header(file, prefix + offset, map, chip, maker);
    assert(!fclose(file));
}

int main(void)
{
    char path[] = "/tmp/snemul-rom-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    static const struct { unsigned map, chip, maker; ShimVariant want; } cases[] = {
        {0x20, 0x03, 0x01, SHIM_DSP},  /* Pilotwings */
        {0x20, 0x05, 0x01, SHIM_STABLE}, /* Dungeon Master: DSP-2 */
        {0x21, 0x03, 0x01, SHIM_DSP},  /* HiROM DSP-1 */
        {0x21, 0x05, 0x01, SHIM_DSP},
        {0x30, 0x03, 0x01, SHIM_STABLE}, /* Top Gear 3000: DSP-4 */
        {0x30, 0x05, 0xB2, SHIM_STABLE}, /* SD Gundam GX: DSP-3 */
        {0x30, 0x05, 0x01, SHIM_DSP},
        {0x31, 0x03, 0x01, SHIM_DSP},
        {0x31, 0x05, 0x01, SHIM_DSP},
        {0x32, 0x43, 0x33, SHIM_SDD1}, /* Street Fighter Alpha/Zero 2 */
        {0x32, 0x45, 0x33, SHIM_SDD1}, /* Star Ocean */
        {0x20, 0x43, 0x33, SHIM_STABLE}, /* mismatched S-DD1 map */
        {0x20, 0x02, 0x01, SHIM_STABLE}, /* Super Mario World */
        {0x20, 0xF3, 0x33, SHIM_STABLE}, /* CX4 */
        {0x23, 0x35, 0x33, SHIM_STABLE}, /* SA-1 */
        {0x20, 0x13, 0x33, SHIM_STABLE}, /* SuperFX */
        {0x3A, 0xF9, 0x33, SHIM_STABLE}, /* SPC7110 */
        {0x35, 0x02, 0x33, SHIM_STABLE}, /* ExHiROM */
        {0x35, 0x03, 0x33, SHIM_STABLE}, /* unsupported DSP mapping */
        {0x23, 0x03, 0x33, SHIM_STABLE},
    };
    for (unsigned prefix = 0; prefix <= 512; prefix += 512)
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        unsigned map = cases[i].map & ~0x10u;
        long offset = map == 0x25 ? 0x40FFC0 : map == 0x21 || map == 0x2A ? 0xFFC0 : 0x7FC0;
        rom(path, offset == 0x40FFC0 ? 0x600000 : 0x100000, prefix, offset,
            cases[i].map, cases[i].chip, cases[i].maker);
        assert(shim_rom_variant(path) == cases[i].want);
    }
    /* A patched body needs no full-ROM checksum, and filenames never select. */
    rom(path, 0x100000, 0, 0x7FC0, 0x20, 0x03, 1);
    FILE *file = fopen(path, "r+b"); assert(file);
    assert(fputc(42, file) == 42); assert(!fclose(file));
    assert(shim_rom_variant(path) == SHIM_DSP);
    /* Valid stable HiROM must beat fake special-chip bytes in LoROM data. */
    file = fopen(path, "r+b"); assert(file);
    header(file, 0x7FC0, 0x21, 0x03, 1); /* wrong location */
    header(file, 0xFFC0, 0x21, 0x02, 1);
    assert(!fclose(file)); assert(shim_rom_variant(path) == SHIM_STABLE);
    /* Equally plausible contradictory headers are unknown, not a guess. */
    file = fopen(path, "r+b"); assert(file);
    header(file, 0x7FC0, 0x20, 0x03, 1);
    assert(!fclose(file)); assert(shim_rom_variant(path) == SHIM_STABLE);
    /* Truncated, corrupt, missing and non-ROM inputs retain stable behavior. */
    rom(path, 0x100000, 0, 0x7FC0, 0x20, 0x03, 1);
    file = fopen(path, "r+b"); assert(file);
    assert(!fseek(file, 0x7FDC, SEEK_SET)); assert(fputc(0, file) == 0);
    assert(!fclose(file)); assert(shim_rom_variant(path) == SHIM_STABLE);
    assert(!truncate(path, 0x7FFF)); assert(shim_rom_variant(path) == SHIM_STABLE);
    assert(!remove(path)); assert(shim_rom_variant(path) == SHIM_STABLE);
    assert(shim_rom_variant(NULL) == SHIM_STABLE && shim_rom_variant("") == SHIM_STABLE);

    ShimConfig config; shim_config_defaults(&config);
    for (unsigned enabled = 0; enabled < 2; ++enabled)
    for (unsigned dsi = 0; dsi < 2; ++dsi)
    for (unsigned mode = 0; mode < SHIM_MODE_COUNT; ++mode)
    for (unsigned variant = SHIM_STABLE; variant <= SHIM_SDD1; ++variant) {
        config.auto_version = enabled;
        const char *want;
        bool twl = mode == SHIM_AUTO && dsi;
        if (mode == SHIM_LEGACY) want = "SNEmulDS_0.6a.nds";
        else if (enabled && variant == SHIM_DSP) want = twl ? "SNEmulDS_dsp.srl" : "SNEmulDS_dsp.nds";
        else if (enabled && variant == SHIM_SDD1) want = twl ? "SNEmulDS_sdd1.srl" : "SNEmulDS_sdd1.nds";
        else want = twl ? "SNEmulDS.srl" : "SNEmulDS.nds";
        assert(!strcmp(shim_emulator_path(&config, mode, dsi, variant), want));
    }
    puts("PASS: ROM header selection, copier headers, DSP subtype exclusions, ambiguous/corrupt input and mode matrix");
}
