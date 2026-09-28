/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHIM_H
#define SHIM_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* TGDS 1.65 has 128-byte slots and writes a second trailing NUL. */
#define SHIM_PATH_CAP 128
#define SHIM_PATH_MAX 126
#define SHIM_WIRE_CAP 236 /* 256-byte TGDS save area minus 20-byte header */

bool shim_path(const char *input, const char *cwd, char out[SHIM_PATH_CAP]);
const char *shim_device(const char *path);
bool shim_tgds_path(const char *input, char out[SHIM_PATH_CAP]);
bool shim_directory(const char *path, char out[SHIM_PATH_CAP]);
typedef enum { SHIM_AUTO, SHIM_NTR, SHIM_LEGACY, SHIM_MODE_COUNT } ShimMode;
typedef struct {
    char ntr[SHIM_PATH_CAP], twl[SHIM_PATH_CAP], legacy[SHIM_PATH_CAP];
    bool autoboot;
    ShimMode default_mode;
} ShimConfig;
void shim_config_defaults(ShimConfig *config);
const char *shim_config_read(FILE *file, ShimConfig *config, unsigned *line);
const char *shim_mode_key(ShimMode mode);
bool shim_target_twl(ShimMode mode, bool dsi);
unsigned shim_menu_modes(bool dsi, ShimMode modes[SHIM_MODE_COUNT]);
const char *shim_storage_check(ShimMode mode, bool dsi, const char *device);
const char *shim_save_preferences(const char *path, const ShimConfig *config);
const char *shim_legacy_config(const char *path, const char *rom);
/* Shared tested text editor; NULL section means keys before any section. */
const char *shim_ini_edit(FILE *in, FILE *out, const char *section,
                          const char *const *keys, const char *const *values, unsigned count);
const char *shim_check_header(const unsigned char *h, size_t header_size,
                              uint32_t file_size, bool dsi);
bool shim_is_rom(const char *path);
size_t shim_pack(const char *const args[3], unsigned char out[SHIM_WIRE_CAP]);
bool shim_dldi_relocate(void *data, size_t capacity, uint32_t target);
size_t shim_tgds_dldi_patch_offset(const void *data, size_t size);
const char *chainload_check(const char *filename, bool twl);
const char *chainload(const char *filename, const unsigned char *args, size_t length, bool twl);
#endif
