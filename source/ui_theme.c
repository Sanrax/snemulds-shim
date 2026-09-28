/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <nds.h>
#include "ui.h"

void ui_init(void)
{
    PrintConsole *con = consoleDemoInit();
    /* Default 1bpp font is expanded by libnds into 4bpp VRAM tiles.
     * Make blank pixels opaque palette index 1. A dedicated palette can
     * then highlight the whole selected row, including spaces. Use 16-bit
     * VRAM writes; foreground pixels remain at palette index 15. */
    volatile u16 *tiles = con->fontBgGfx + con->fontCharOffset * 16;
    for (unsigned i = 0; i < (unsigned)con->font.numChars * 16; ++i) {
        u16 pixels = tiles[i];
        for (unsigned shift = 0; shift < 16; shift += 4)
            if (!(pixels & (15u << shift))) pixels |= 1u << shift;
        tiles[i] = pixels;
    }
    const u16 background = RGB15(0, 0, 0);
    BG_PALETTE_SUB[0] = background;
    for (unsigned p = 0; p < 16; ++p) {
        BG_PALETTE_SUB[p * 16 + 1] = background;
        BG_PALETTE_SUB[p * 16 + 15] = RGB15(29, 30, 31);
    }
    BG_PALETTE_SUB[UI_MUTED * 16 + 15] = RGB15(16, 19, 23);
    BG_PALETTE_SUB[UI_ACCENT * 16 + 15] = RGB15(11, 26, 30);
    BG_PALETTE_SUB[UI_GOOD * 16 + 15] = RGB15(15, 28, 19);
    BG_PALETTE_SUB[UI_ERROR * 16 + 15] = RGB15(31, 21, 12);
    BG_PALETTE_SUB[UI_SELECTED * 16 + 1] = RGB15(4, 9, 15);
    BG_PALETTE_SUB[UI_HEADER * 16 + 15] = RGB15(11, 26, 30);
}
