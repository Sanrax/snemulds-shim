/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"

static unsigned read16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

/* Validate the location as well as the header. Looking only at chipset bytes
 * in a LoROM file's HiROM data (or vice versa) can select an unrelated build.
 * Check the stored checksum pair, not a checksum of the entire ROM: renamed,
 * translated and patched ROMs can retain their original cartridge header.
 */
static unsigned header_score(const unsigned char h[64], unsigned location)
{
    unsigned map = h[0x15] & ~0x10u; /* SlowROM/FastROM share a mapping. */
    if (location == 0 && map != 0x20 && map != 0x22 && map != 0x23) return 0;
    if (location == 1 && map != 0x21 && map != 0x2A) return 0;
    if (location == 2 && map != 0x25) return 0;
    if (h[0x17] < 7 || h[0x17] > 16 || h[0x18] > 9 ||
        read16(h + 0x3C) < 0x8000 ||
        (read16(h + 0x1C) ^ read16(h + 0x1E)) != 0xFFFF)
        return 0;
    unsigned score = 1;
    /* ASCII and JIS X 0201 titles; a NUL at the end is also common. */
    for (unsigned i = 0; i < 21; ++i)
        if (h[i] && (h[i] < 0x20 || (h[i] > 0x7E && h[i] < 0xA1) || h[i] > 0xDF))
            return score;
    return score + 1;
}

static ShimVariant header_variant(const unsigned char h[64])
{
    unsigned map = h[0x15], chip = h[0x16];
    bool dsp_map = map == 0x20 || map == 0x21 || map == 0x30 || map == 0x31;
    /* Snes9x's DSP subtype rules distinguish DSP-1 from Dungeon Master
     * (DSP-2), SD Gundam GX (DSP-3) and Top Gear 3000 (DSP-4).
     * https://github.com/snes9xgit/snes9x/blob/master/memmap.cpp */
    if (dsp_map && ((chip == 0x03 && map != 0x30) ||
        (chip == 0x05 && map != 0x20 && !(map == 0x30 && h[0x1A] == 0xB2))))
        return SHIM_DSP;
    if (map == 0x32 && (chip == 0x43 || chip == 0x45)) return SHIM_SDD1;
    return SHIM_STABLE;
}

ShimVariant shim_rom_variant(const char *path)
{
    if (!path || !*path) return SHIM_STABLE;
    FILE *file = fopen(path, "rb");
    if (!file) return SHIM_STABLE;
    ShimVariant variant = SHIM_STABLE;
    unsigned best = 0;
    if (!fseek(file, 0, SEEK_END)) {
        long size = ftell(file);
        if (size >= 0x8000 && (!(size & 0x7FFF) || (size & 0x7FFF) == 512)) {
            long prefix = (size & 0x7FFF) == 512 ? 512 : 0;
            static const long offsets[] = {0x7FC0, 0xFFC0, 0x40FFC0};
            for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
                unsigned char h[64];
                long offset = prefix + offsets[i];
                if (offset > size - (long)sizeof(h)) continue;
                if (fseek(file, offset, SEEK_SET) || fread(h, 1, sizeof(h), file) != sizeof(h)) {
                    variant = SHIM_STABLE;
                    break;
                }
                unsigned score = header_score(h, i);
                ShimVariant candidate = header_variant(h);
                if (score > best) { best = score; variant = candidate; }
                else if (score && score == best && candidate != variant)
                    variant = SHIM_STABLE; /* Ambiguous headers: use stable. */
            }
        }
    }
    if (fclose(file)) return SHIM_STABLE;
    return variant;
}

const char *shim_variant_name(ShimVariant variant)
{
    return variant == SHIM_DSP ? "DSP-1" : variant == SHIM_SDD1 ? "S-DD1" : "Stable";
}

const char *shim_emulator_path(const ShimConfig *config, ShimMode mode, bool dsi,
                               ShimVariant variant)
{
    if (mode == SHIM_LEGACY) return config->legacy;
    bool twl = shim_target_twl(mode, dsi);
    if (config->auto_version) {
        if (variant == SHIM_DSP) return twl ? config->dsp_twl : config->dsp_ntr;
        if (variant == SHIM_SDD1) return twl ? config->sdd1_twl : config->sdd1_ntr;
    }
    return twl ? config->twl : config->ntr;
}
