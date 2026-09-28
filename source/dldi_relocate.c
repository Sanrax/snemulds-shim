/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"
#include <string.h>

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write32(unsigned char *p, uint32_t x)
{
    p[0] = x; p[1] = x >> 8; p[2] = x >> 16; p[3] = x >> 24;
}

bool shim_dldi_relocate(void *data, size_t capacity, uint32_t target)
{
    unsigned char *b = data;
    if (capacity < 128 || b[13] < 7 || b[13] > 15) return false;
    size_t size = 1u << b[13];
    if (size > capacity) return false;
    uint32_t old = read32(b + 0x40), delta = target - old;
    if (old > UINT32_MAX - size || target > UINT32_MAX - capacity) return false;
    uint32_t starts[4], ends[4];
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t a = read32(b + 0x40 + i * 8), z = read32(b + 0x44 + i * 8);
        starts[i] = a - old; ends[i] = z - old;
        if ((b[14] & (1u << i)) &&
            (a < old || z < a || z - old > size || (a & 3) || (z & 3))) return false;
    }
    /* Merge overlapping FIX_ALL/FIX_GLUE/FIX_GOT ranges so each word is fixed once. */
    for (size_t i = 0x80; i + 4 <= size; i += 4) {
        bool fix = false;
        for (unsigned section = 0; section < 3; ++section)
            if ((b[14] & (1u << section)) && i >= starts[section] && i < ends[section]) fix = true;
        uint32_t p = read32(b + i);
        if (fix && p >= old && p - old < size) write32(b + i, p + delta);
    }
    for (unsigned i = 0x40; i < 0x60; i += 4) write32(b + i, read32(b + i) + delta);
    for (unsigned i = 0x68; i < 0x80; i += 4) write32(b + i, read32(b + i) + delta);
    if (b[14] & 8) memset(b + starts[3], 0, ends[3] - starts[3]);
    return true;
}
