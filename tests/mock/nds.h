/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#define KEY_A (1u << 0)
#define KEY_B (1u << 1)
#define KEY_SELECT (1u << 2)
#define KEY_START (1u << 3)
#define KEY_RIGHT (1u << 4)
#define KEY_LEFT (1u << 5)
#define KEY_UP (1u << 6)
#define KEY_DOWN (1u << 7)
#define KEY_R (1u << 8)
#define KEY_L (1u << 9)
void consoleDemoInit(void);
void consoleClear(void);
void swiWaitForVBlank(void);
void scanKeys(void);
unsigned keysDown(void);
unsigned keysHeld(void);
bool isDSiMode(void);
char *mock_getcwd(char *buf, size_t size);
