/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TGDS_LANGUAGE_H
#define TGDS_LANGUAGE_H
#include <stdbool.h>
#include <stddef.h>

/* Validate the stock 0.6d ARM9 display switches; optionally patch in RAM. */
bool shim_tgds_language_patch(void *arm9, size_t size, bool apply);

/* TWL only: capture the loader's firmware language before TGDS remaps RAM. */
bool shim_tgds_twl_language_patch(void *arm9, size_t size, unsigned language, bool apply);

#endif
