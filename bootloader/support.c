/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Freestanding loader helpers. Byte updates use halfwords for VRAM safety. */
#include <stddef.h>
#include <stdint.h>
#include <nds/dma.h>

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

static void putbyte(unsigned char *p, unsigned char value)
{
    uintptr_t address = (uintptr_t)p;
    volatile uint16_t *word = (volatile uint16_t *)(address & ~(uintptr_t)1);
    unsigned shift = (address & 1) * 8;
    *word = (*word & ~(0xFFu << shift)) | ((unsigned)value << shift);
}
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; ++i) putbyte(d + i, s[i]);
    return dst;
}
void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    for (size_t i = 0; i < n; ++i) putbyte(d + i, (unsigned char)c);
    return dst;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
