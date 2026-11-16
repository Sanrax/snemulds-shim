/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tgds_language.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    enum { SIZE = 0x5BD64, SITE = 0x27598 };
    const uint32_t stock[] = {0xE59F3008, 0xE5D30535, 0xE2000007, 0xE12FFF1E, 0x02FFF0FF};
    unsigned char *image = calloc(1, SIZE), *before = malloc(SIZE);
    assert(image && before);
    for (unsigned i = 0; i < sizeof(stock) / sizeof(stock[0]); ++i)
        for (unsigned b = 0; b < 4; ++b)
            image[SITE + i * 4 + b] = stock[i] >> (b * 8);
    memcpy(before, image, SIZE);
    assert(!shim_tgds_twl_language_patch(NULL, SIZE, 1, true));
    assert(!shim_tgds_twl_language_patch(image, 0, 1, true));
    assert(!shim_tgds_twl_language_patch(image, SITE + sizeof(stock) - 1, 1, true));
    assert(!shim_tgds_twl_language_patch(image, SIZE - 1, 1, true));
    assert(!shim_tgds_twl_language_patch(image, 0x5C3C0, 1, true)); /* NTR */
    assert(!shim_tgds_twl_language_patch(image, SIZE, 8, true));
    assert(!shim_tgds_twl_language_patch(image, SIZE, (unsigned)-1, true));
    assert(!memcmp(image, before, SIZE));
    for (unsigned i = 0; i < sizeof(stock); ++i) {
        image[SITE + i] ^= 1;
        assert(!shim_tgds_twl_language_patch(image, SIZE, 1, true));
        image[SITE + i] ^= 1;
        assert(!memcmp(image, before, SIZE));
    }
    for (unsigned language = 0; language < 8; ++language) {
        memcpy(image, before, SIZE);
        assert(shim_tgds_twl_language_patch(image, SIZE, language, false));
        assert(!memcmp(image, before, SIZE));
        assert(shim_tgds_twl_language_patch(image, SIZE, language, true));
        const unsigned char expected[] = {language, 0, 0xA0, 0xE3, 0x1E, 0xFF, 0x2F, 0xE1};
        assert(!memcmp(image + SITE, expected, sizeof(expected)));
        assert(!memcmp(image, before, SITE));
        assert(!memcmp(image + SITE + 8, before + SITE + 8, SIZE - SITE - 8));
        memcpy(before + SITE, expected, sizeof(expected));
        assert(!shim_tgds_twl_language_patch(image, SIZE, language, true));
        assert(!memcmp(image, before, SIZE));
        for (unsigned i = 0; i < 8; ++i)
            before[SITE + i] = stock[i / 4] >> ((i % 4) * 8);
    }
    free(before);
    free(image);
    puts("PASS: TWL language validation, RAM-only writes and all firmware IDs");
    return 0;
}
