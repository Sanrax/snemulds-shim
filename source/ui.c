/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

/* Use explicit cells: no auto-wrapping can scroll a footer off the screen.
 * Paths are printed as data, with control/non-font bytes replaced by '?'. */
void ui_text(unsigned row, unsigned col, UiStyle style, const char *text)
{
    if (row >= 24 || col >= 32) return;
    printf("\x1b[%u;%uH\x1b[%um", row, col, 30u + style);
    for (unsigned i = col; i < 31 && *text && *text != '\n'; ++i) {
        unsigned char c = (unsigned char)*text++;
        putchar(c >= 32 && c <= 126 ? c : '?');
    }
}

void ui_bar(unsigned row, UiStyle style, const char *text)
{
    char line[31];
    memset(line, ' ', 30); line[30] = 0;
    size_t n = strlen(text); if (n > 30) n = 30;
    memcpy(line, text, n);
    ui_text(row, 1, style, line);
}

void ui_begin(const char *title)
{
    printf("\x1b[37m"); consoleClear();
    ui_bar(0, UI_HEADER, title);
}

void ui_section(unsigned row, const char *title)
{
    char line[31];
    memset(line, '-', 30); line[30] = 0;
    size_t n = strlen(title); if (n > 28) n = 28;
    memcpy(line, title, n); line[n] = ' ';
    ui_text(row, 1, UI_ACCENT, line);
}

void ui_wrap(unsigned row, unsigned rows, UiStyle style, const char *text)
{
    for (unsigned i = 0; i < rows && *text; ++i) {
        char line[31]; size_t n = 0;
        memset(line, ' ', 30);
        while (n < 30 && *text && *text != '\n') line[n++] = *text++;
        if (*text == '\n') ++text;
        if (i + 1 == rows && *text) { memcpy(line + 27, "...", 3); n = 30; }
        line[n] = 0;
        ui_text(row + i, 1, style, line);
    }
}

void ui_footer(const char *first, const char *second)
{
    ui_text(20, 1, UI_MUTED, "------------------------------");
    ui_text(21, 1, UI_TEXT, first);
    ui_text(22, 1, UI_MUTED, second);
}
