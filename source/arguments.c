/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"
#include <string.h>
#include <ctype.h>

static const char *strip_device(const char *p)
{
    if (!strncmp(p, "fat:/", 5)) return p + 4;
    if (!strncmp(p, "sd:/", 4)) return p + 3;
    if (!strncmp(p, "0:/", 3)) return p + 2;
    if (strchr(p, ':')) return NULL;
    return p;
}

static bool append_parts(const char *p, char *out, size_t *n, size_t root)
{
    while (*p) {
        while (*p == '/') ++p;
        const char *start = p;
        while (*p && *p != '/') {
            if ((unsigned char)*p < 32 || *p == '\\' || *p == ':') return false;
            ++p;
        }
        size_t len = (size_t)(p - start);
        if (!len || (len == 1 && start[0] == '.')) continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (*n == root) return false; /* Above the filesystem root */
            while (*n > root && out[*n - 1] != '/') --*n;
            if (*n > root) --*n;
            out[*n] = 0;
            continue;
        }
        size_t sep = *n > root;
        if (*n + sep + len + (5 - root) > SHIM_PATH_MAX) return false;
        if (sep) out[(*n)++] = '/';
        memcpy(out + *n, start, len);
        *n += len;
        out[*n] = 0;
    }
    return true;
}

bool shim_path(const char *input, const char *cwd, char out[SHIM_PATH_CAP])
{
    if (!input || !*input) return false;
    const char *p = strip_device(input);
    if (!p) return false;
    bool sd = !strncmp(input, "sd:/", 4) ||
              (strncmp(input, "fat:/", 5) && cwd && !strncmp(cwd, "sd:/", 4));
    strcpy(out, sd ? "sd:/" : "fat:/");
    size_t root = strlen(out), n = root;
    if (*p != '/') {
        const char *base = cwd ? strip_device(cwd) : NULL;
        if (!base || *base != '/' || !append_parts(base, out, &n, root)) return false;
    }
    return append_parts(p, out, &n, root) && n > root;
}

const char *shim_device(const char *path)
{
    if (path && !strncmp(path, "fat:/", 5)) return "fat:/";
    if (path && !strncmp(path, "sd:/", 4)) return "sd:/";
    return NULL;
}

bool shim_is_rom(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext || strlen(ext) != 4) return false;
    return tolower((unsigned char)ext[1]) == 's' &&
           ((tolower((unsigned char)ext[2]) == 'f' && tolower((unsigned char)ext[3]) == 'c') ||
            (tolower((unsigned char)ext[2]) == 'm' && tolower((unsigned char)ext[3]) == 'c'));
}

size_t shim_pack(const char *const args[3], unsigned char out[SHIM_WIRE_CAP])
{
    size_t n = 0;
    memset(out, 0, SHIM_WIRE_CAP);
    for (unsigned i = 0; i < 3; ++i) {
        if (!args[i]) return 0;
        size_t len = strlen(args[i]);
        if (len <= 3 || len > SHIM_PATH_MAX || n + len + 1 > SHIM_WIRE_CAP) return 0;
        memcpy(out + n, args[i], len + 1);
        n += len + 1;
    }
    /* Length includes exactly three NULs, with no extra empty argument. */
    return n;
}

bool shim_directory(const char *path, char out[SHIM_PATH_CAP])
{
    if (!path || strlen(path) >= SHIM_PATH_CAP) return false;
    const char *slash = strrchr(path, '/');
    if (!slash) return false;
    size_t n = slash - path + 1;
    memcpy(out, path, n);
    out[n] = 0;
    return true;
}

bool shim_tgds_path(const char *input, char out[SHIM_PATH_CAP])
{
    const char *p = input ? strip_device(input) : NULL;
    if (!p || *p != '/' || strlen(p) + 4 > SHIM_PATH_MAX) return false;
    strcpy(out, "fat:");
    strcpy(out + 4, p);
    return true;
}
