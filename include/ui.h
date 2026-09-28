/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHIM_UI_H
#define SHIM_UI_H

typedef enum {
    UI_MUTED = 0, UI_ERROR = 1, UI_GOOD = 2,
    UI_SELECTED = 4, UI_HEADER = 5, UI_ACCENT = 6, UI_TEXT = 7
} UiStyle;

void ui_init(void);
void ui_begin(const char *title);
void ui_text(unsigned row, unsigned col, UiStyle style, const char *text);
void ui_bar(unsigned row, UiStyle style, const char *text);
void ui_section(unsigned row, const char *title);
void ui_wrap(unsigned row, unsigned rows, UiStyle style, const char *text);
void ui_footer(const char *first, const char *second);
void ui_versions(unsigned scroll);
unsigned ui_versions_scroll_limit(void);

#endif
