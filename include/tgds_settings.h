/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TGDS_SETTINGS_H
#define TGDS_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>

/* Restore stable 0.6d's config read; validate the alternates' native reader
 * without changing it. All edits are confined to the loaded ARM9 image. */
bool shim_tgds_game_settings_patch(void *arm9, size_t size, bool apply);
bool shim_tgds_macro_timer_patch(void *arm9, size_t size, bool apply);

#endif
