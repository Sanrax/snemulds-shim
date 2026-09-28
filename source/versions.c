/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdio.h>
#include "ui.h"

/* User-supplied explanations, word-wrapped to the 30-column content area.
 * Scrolling preserves every word while keeping navigation visible. */
static const struct { const char *text; bool heading; } lines[] = {
    {"SNEmulDS 0.6d TWL", true},
    {"DSi mode-enabled version of", false},
    {"Cotodevel's 0.6d SNEmulDS", false},
    {"fork. Adds support for the", false},
    {"Megaman X series by enabling", false},
    {"CX4 co-processor support, and", false},
    {"adds argv support. The TWL", false},
    {"version also has extended RAM", false},
    {"that improves compatibility.", false},
    {"", false},
    {"SNEmulDS 0.6d NTR", true},
    {"DS-mode version of 0.6d fork", false},
    {"with the same feature set.", false},
    {"Auto mode boots this version", false},
    {"when booted in DS-mode", false},
    {"environments. Compatibility", false},
    {"is similar, but slightly worse", false},
    {"than the 0.6d TWL version.", false},
    {"", false},
    {"SNEmulDS 0.6a", true},
    {"Legacy build of SNEmulDS by", false},
    {"Archeide. Only DS mode is", false},
    {"supported, but compatibility", false},
    {"can be better in some cases", false},
    {"than 0.6d. Try this version if", false},
    {"a game in 0.6d isn't working", false},
    {"well.", false},
};
enum { VISIBLE_LINES = 17, LINE_COUNT = sizeof(lines) / sizeof(lines[0]) };

unsigned ui_versions_scroll_limit(void)
{
    return LINE_COUNT > VISIBLE_LINES ? LINE_COUNT - VISIBLE_LINES : 0;
}

void ui_versions(unsigned scroll)
{
    if (scroll > ui_versions_scroll_limit()) scroll = ui_versions_scroll_limit();
    ui_begin("SNEmulDS Versions          1/4");
    for (unsigned i = 0; i < VISIBLE_LINES && i + scroll < LINE_COUNT; ++i) {
        if (lines[i + scroll].heading) ui_section(i + 2, lines[i + scroll].text);
        else ui_text(i + 2, 1, UI_TEXT, lines[i + scroll].text);
    }
    char position[31];
    unsigned end = scroll + VISIBLE_LINES;
    if (end > LINE_COUNT) end = LINE_COUNT;
    snprintf(position, sizeof(position), "Lines %u-%u of %u", scroll + 1, end, LINE_COUNT);
    ui_text(19, 1, UI_MUTED, position);
    ui_footer("L  Menu            R  Next", "UP/DOWN  Scroll text");
}
