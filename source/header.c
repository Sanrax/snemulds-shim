/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static bool overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    return n && m && a < b + m && b < a + n;
}

const char *shim_check_header(const unsigned char *h, size_t header_size,
                              uint32_t file_size, bool dsi)
{
    if (header_size < 512 || file_size < 512) return "Truncated NDS header.";
    if (h[0x12] != (dsi ? 2 : 0))
        return dsi ? "Use the TWL SnemulDS .srl build." : "Use the NTR SnemulDS .nds build.";
    if (dsi && file_size < 0x1000) return "Truncated TWL header.";
    uint32_t dst[5] = {0}, len[5] = {0};
    const unsigned offsets[] = {0x20, 0x30, 0x1c0, 0x1d0};
    for (unsigned i = 0; i < (dsi ? 4u : 2u); ++i) {
        const unsigned char *p = h + offsets[i];
        uint32_t off = read32(p);
        dst[i] = read32(p + 8); len[i] = read32(p + 12);
        if (i >= 2 && !len[i]) continue;
        uint32_t floor = i < 2 ? 0x02000000 : 0x02400000;
        uint32_t ceiling = i == 0 ? 0x02380000 : i == 1 ? 0x023F0000 : 0x02FF0000;
        /* Old libnds (0.6a) puts ARM7 in shared WRAM + IWRAM. The loader
         * runs in VRAM; keep the upper IRQ-vector area reserved. */
        if (!dsi && i == 1 && dst[i] >= 0x037F8000) {
            floor = 0x037F8000;
            ceiling = 0x0380F000;
        }
        if (off < (dsi ? 0x1000u : 512u) || off > file_size || len[i] > file_size - off ||
            dst[i] < floor || dst[i] >= ceiling || !len[i] ||
            (dst[i] & 3) || (len[i] & 3) || len[i] > ceiling - dst[i])
            return "Unsupported/corrupt TGDS layout.";
        if (i < 2) {
            uint32_t entry = read32(p + 4) & ~1u;
            if (entry < dst[i] || entry - dst[i] >= len[i]) return "Invalid entry point.";
        }
        /* Keep TGDS's legacy header mirror available in 16 MiB mode. */
        if (overlap(dst[i], len[i], 0x027FF000, 0x1000)) return "Image overlaps boot data.";
    }
    /* Arguments stay in the first 4 MiB even when ARM9i uses extended RAM. */
    dst[4] = dst[0] + len[0]; len[4] = SHIM_WIRE_CAP;
    if (dst[4] > 0x02380000 - len[4]) return "No room for arguments.";
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (overlap(dst[i], len[i], dst[j], len[j])) return "Overlapping NDS sections.";
    return NULL;
}
