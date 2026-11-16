/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TGDS_IO_H
#define TGDS_IO_H
#include <stddef.h>
#include <stdbool.h>

/* Stock stable/DSP-1/S-DD1 TWL ARM9; checks complete functions before edits.
 * Applies only when the loader has positively identified a PICO DLDI device.
 */
bool shim_tgds_dspico_io(void *arm9, size_t size, bool apply);
/* TWL input fixes apply to both flashcart and internal-SD launches. */
bool shim_tgds_twl_touch(void *arm7, size_t size, bool apply);
#endif
