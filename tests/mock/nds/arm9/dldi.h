/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
typedef struct { struct { unsigned ioType; } ioInterface; } DLDI_INTERFACE;
extern const DLDI_INTERFACE *io_dldi_data;
bool dldiIsValid(const DLDI_INTERFACE *p);
