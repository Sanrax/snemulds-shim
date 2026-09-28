/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TGDS_IO_H
#define TGDS_IO_H
#include <stddef.h>
#include <stdbool.h>

/* Exact stock 0.6d TWL ARM9 image; checks both complete functions before edits.
 * Applies only when the loader has positively identified a PICO DLDI device.
 */
bool shim_tgds_dspico_io(void *arm9, size_t size, bool apply);
/* TWL input fixes apply to both flashcart and internal-SD launches. */
bool shim_tgds_twl_touch(void *arm7, size_t size, bool apply);
#endif
