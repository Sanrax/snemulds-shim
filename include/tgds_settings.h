/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TGDS_SETTINGS_H
#define TGDS_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>

/* Restore stock 0.6d's per-ROM config read in the loaded ARM9 image. */
bool shim_tgds_game_settings_patch(void *arm9, size_t size, bool apply);

#endif
