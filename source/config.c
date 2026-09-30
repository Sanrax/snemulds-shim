/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"
#include <ctype.h>
#include <string.h>

void shim_config_defaults(ShimConfig *config)
{
    /* Relative defaults follow the filesystem and folder of the shim. */
    strcpy(config->ntr, "SNEmulDS.nds");
    strcpy(config->twl, "SNEmulDS.srl");
    strcpy(config->legacy, "SNEmulDS_0.6a.nds");
    config->autoboot = false;
    config->skip_macro_timer = false;
    config->default_mode = SHIM_AUTO;
}

static char *trim(char *p)
{
    while (isspace((unsigned char)*p)) ++p;
    size_t n = strlen(p);
    while (n && isspace((unsigned char)p[n - 1])) p[--n] = 0;
    return p;
}

static bool equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
    }
    return *a == *b;
}

const char *shim_config_read(FILE *file, ShimConfig *config, unsigned *line)
{
    char buf[512];
    bool section = true;
    bool seen[6] = {false};
    *line = 0;
    while (fgets(buf, sizeof(buf), file)) {
        ++*line;
        if (!strchr(buf, '\n') && !feof(file)) return "INI line too long.";
        char *p = buf;
        if (*line == 1 && !strncmp(p, "\xEF\xBB\xBF", 3)) p += 3;
        p = trim(p);
        if (!*p || *p == ';' || *p == '#') continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) return "Invalid INI section.";
            *end = 0;
            char *tail = trim(end + 1);
            if (*tail && *tail != ';' && *tail != '#') return "Invalid INI section.";
            section = equal(trim(p + 1), "snemulds");
            continue;
        }
        if (!section) continue;
        char *eq = strchr(p, '=');
        if (!eq) return "Expected key = path in INI.";
        *eq = 0;
        char *key = trim(p), *value = trim(eq + 1), *dst = NULL;
        unsigned index;
        if (equal(key, "ntr_path")) { dst = config->ntr; index = 0; }
        else if (equal(key, "twl_path")) { dst = config->twl; index = 1; }
        else if (equal(key, "legacy_path")) { dst = config->legacy; index = 2; }
        else if (equal(key, "autoboot")) index = 3;
        else if (equal(key, "default")) index = 4;
        else if (equal(key, "skip_macro_timer")) index = 5;
        else return "Unknown INI key.";
        if (seen[index]) return "Duplicate INI key.";
        if (*value == '"') {
            char *end = strchr(++value, '"');
            if (!end) return "Unclosed quote in INI.";
            *end = 0;
            char *tail = trim(end + 1);
            if (*tail && *tail != ';' && *tail != '#') return "Text after quoted INI path.";
        }
        if (index == 3 || index == 5) {
            bool *option = index == 3 ? &config->autoboot : &config->skip_macro_timer;
            if (equal(value, "true") || equal(value, "on") || equal(value, "1")) *option = true;
            else if (equal(value, "false") || equal(value, "off") || equal(value, "0")) *option = false;
            else return index == 3 ? "autoboot must be true or false." :
                                     "skip_macro_timer must be true or false.";
            seen[index] = true;
            continue;
        }
        if (index == 4) {
            unsigned mode;
            for (mode = 0; mode < SHIM_MODE_COUNT; ++mode)
                if (equal(value, shim_mode_key((ShimMode)mode))) break;
            if (mode == SHIM_MODE_COUNT) return "default must be auto, ntr or legacy.";
            config->default_mode = (ShimMode)mode;
            seen[index] = true;
            continue;
        }
        /* Unquoted paths use the whole line, preserving # and ; in filenames. */
        if (!*value || strlen(value) > SHIM_PATH_MAX) return "Empty/overlong INI path.";
        for (const char *s = value; *s; ++s)
            if ((unsigned char)*s < 32 || *s == '\\') return "Use forward slashes in INI paths.";
        strcpy(dst, value);
        seen[index] = true;
    }
    return ferror(file) ? "Could not read INI." : NULL;
}

const char *shim_mode_key(ShimMode mode)
{
    static const char *const keys[] = {"auto", "ntr", "legacy"};
    return (unsigned)mode < SHIM_MODE_COUNT ? keys[mode] : "invalid";
}

bool shim_target_twl(ShimMode mode, bool dsi)
{
    return mode == SHIM_AUTO && dsi;
}

unsigned shim_menu_modes(bool dsi, ShimMode modes[SHIM_MODE_COUNT])
{
    modes[0] = SHIM_AUTO;
    modes[1] = dsi ? SHIM_NTR : SHIM_LEGACY;
    if (dsi) modes[2] = SHIM_LEGACY;
    return dsi ? 3 : 2;
}

const char *shim_storage_check(ShimMode mode, bool dsi, const char *device)
{
    if (!device || (strcmp(device, "fat:/") && strcmp(device, "sd:/")))
        return "Unsupported storage device.";
    if (!strcmp(device, "sd:/") && !shim_target_twl(mode, dsi))
        return "NTR needs flashcart storage.\nInternal SD is unavailable in\ntrue NTR mode. Use 0.6d Auto\nor launch from your flashcart.";
    return NULL;
}
