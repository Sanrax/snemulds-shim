/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *trim(char *p)
{
    while (isspace((unsigned char)*p)) ++p;
    size_t n = strlen(p);
    while (n && isspace((unsigned char)p[n - 1])) p[--n] = 0;
    return p;
}

static bool equal(const char *a, const char *b)
{
    while (*a && *b)
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
    return *a == *b;
}

static void missing(FILE *out, const char *const *keys, const char *const *values,
                    bool *seen, unsigned count, const char *eol)
{
    for (unsigned i = 0; i < count; ++i) if (!seen[i]) {
        fprintf(out, "%s = %s%s", keys[i], values[i], eol);
        seen[i] = true;
    }
}

const char *shim_ini_edit(FILE *in, FILE *out, const char *section,
                          const char *const *keys, const char *const *values, unsigned count)
{
    if (!count || count > 3) return "Invalid INI edit.";
    /* Shim settings are accepted at top level as well as in [snemulds]. */
    bool active = !section || equal(section, "snemulds"), seen[3] = {false};
    bool first = true, newline = true;
    char raw[1024], parsed[1024];
    const char *eol = "\n";
    size_t bytes = 0;
    while (in) {
        size_t n = 0;
        int c;
        while ((c = fgetc(in)) != EOF) {
            if (!c) return "Config contains NUL; unchanged.";
            if (++bytes > 65536 || n == sizeof(raw) - 1)
                return "Config too large; left unchanged.";
            raw[n++] = (char)c;
            if (c == '\n') break;
        }
        if (!n) break;
        raw[n] = 0;
        if (strstr(raw, "\r\n")) eol = "\r\n";
        strcpy(parsed, raw);
        char *p = parsed;
        bool bom = first && !strncmp(p, "\xEF\xBB\xBF", 3);
        if (bom) {
            p += 3;
            /* Emit before any inserted keys, never in the middle of the file.
             * 0.6a's older config reader does not understand a UTF-8 BOM. */
            if (section) fputs("\xEF\xBB\xBF", out);
        }
        p = trim(p);
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) return "Invalid config section; unchanged.";
            *end = 0;
            char *tail = trim(end + 1);
            if (*tail && *tail != ';' && *tail != '#')
                return "Invalid config section; unchanged.";
            if (active && !section) missing(out, keys, values, seen, count, eol);
            active = section && equal(trim(p + 1), section);
        }
        bool replaced = false;
        if (active && *p != ';' && *p != '#' && *p != '[') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = 0;
                for (unsigned i = 0; i < count; ++i) if (equal(trim(p), keys[i])) {
                    if (seen[i]) return "Duplicate config key; unchanged.";
                    fprintf(out, "%s = %s%s", keys[i], values[i], eol);
                    seen[i] = replaced = true;
                    break;
                }
            }
        }
        if (!replaced) fputs(raw + (bom ? 3 : 0), out);
        newline = replaced || (n && raw[n - 1] == '\n');
        first = false;
    }
    if (in && ferror(in)) return "Cannot read config; unchanged.";
    if (!newline) fputs(eol, out);
    bool need = false;
    for (unsigned i = 0; i < count; ++i) if (!seen[i]) need = true;
    if (need && !active && section) fprintf(out, "%s[%s]%s", eol, section, eol);
    missing(out, keys, values, seen, count, eol);
    return ferror(out) ? "Cannot write config; unchanged." : NULL;
}

static bool exists(const char *path) { struct stat st; return stat(path, &st) == 0; }

/* FAT cannot rename over an existing file. Stage completely, then rename the
 * original aside, commit, and remove only our previous-version transaction.
 * Never truncate the user's file. A .shim-old survives an interrupted commit.
 */
static const char *update(const char *path, const char *section,
                          const char *const *keys, const char *const *values, unsigned count)
{
    char tmp[512], old[512];
    if (snprintf(tmp, sizeof(tmp), "%s.shim-new", path) >= (int)sizeof(tmp) ||
        snprintf(old, sizeof(old), "%s.shim-old", path) >= (int)sizeof(old))
        return "Config filename too long.";
    if (exists(tmp) || exists(old))
        return "Interrupted config save found.\nRecover .shim-old/.shim-new\nfiles before saving again.";
    FILE *in = fopen(path, "rb");
    if (!in && errno != ENOENT) return "Cannot open config; unchanged.";
    bool had_original = in != NULL;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) { if (in) fclose(in); return "Cannot create config temp file."; }
    FILE *out = fdopen(fd, "wb");
    if (!out) {
        close(fd); remove(tmp); if (in) fclose(in);
        return "Cannot open config temp file.";
    }
    const char *error = shim_ini_edit(in, out, section, keys, values, count);
    if (in && fclose(in) && !error) error = "Cannot close config input.";
    if (fclose(out) && !error) error = "Cannot flush config; unchanged.";
    if (error) { remove(tmp); return error; }
    if (had_original && rename(path, old)) {
        remove(tmp); return "Cannot back up config; unchanged.";
    }
    if (rename(tmp, path)) {
        if (had_original && rename(old, path))
            return "Config commit failed. Recover\nthe original from .shim-old.";
        remove(tmp);
        return "Config commit failed; restored.";
    }
    if (had_original && remove(old))
        return "Saved, but .shim-old remains.\nRemove that backup before\nchanging settings again.";
    return NULL;
}

const char *shim_save_preferences(const char *path, const ShimConfig *config)
{
    const char *keys[] = {"autoboot", "default", "skip_macro_timer"};
    const char *values[] = {config->autoboot ? "true" : "false",
                            shim_mode_key(config->default_mode),
                            config->skip_macro_timer ? "true" : "false"};
    return update(path, "snemulds", keys, values, 3);
}

const char *shim_legacy_config(const char *path, const char *rom)
{
    char folder[SHIM_PATH_CAP];
    if (!shim_directory(rom, folder)) return "Invalid ROM folder.";
    char *value = strchr(folder, ':');
    if (!value) return "Invalid ROM device.";
    ++value;
    size_t n = strlen(value);
    if (n > 1 && value[n - 1] == '/') value[n - 1] = 0;
    const char *keys[] = {"ROMPath"}, *values[] = {value};
    /* Attached 0.6a calls get_config_string(NULL, "ROMPath", "/SNES/"):
     * this is the unsectioned key, NOT 0.6d's [Global] ROMPath. */
    return update(path, NULL, keys, values, 1);
}
