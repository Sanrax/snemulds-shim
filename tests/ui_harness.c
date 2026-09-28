/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Run the actual menu/main code against temporary host files and key events.
 * Only hardware calls and the final CPU reset are substituted. */
#include <stddef.h>
#define main shim_main
#define getcwd mock_getcwd
#include "../source/main.c"
#undef main
#undef getcwd

static const DLDI_INTERFACE pico = {{0x4F434950}};
const DLDI_INTERFACE *io_dldi_data = &pico;
static const char *events;
static unsigned current, scans, frames;
void consoleDemoInit(void) {}
void ui_init(void) {}
void consoleClear(void) { puts("\n<CLEAR>"); }
bool dldiIsValid(const DLDI_INTERFACE *p) { return p != NULL; }
bool fatInitDefault(void) { return true; }
bool isDSiMode(void) { return !strcmp(getenv("TEST_DSI"), "1"); }
char *mock_getcwd(char *buf, size_t size)
{
    snprintf(buf, size, "%s/", getenv("TEST_DEVICE")); return buf;
}
void swiWaitForVBlank(void) { if (++frames > 100) abort(); }
void scanKeys(void)
{
    current = 0;
    if (++scans <= 3) return; /* startup samples */
    if (!*events) { current = KEY_START; return; }
    switch (*events++) {
        case 'A': current = KEY_A; break; case 'B': current = KEY_B; break;
        case 'D': current = KEY_DOWN; break; case 'U': current = KEY_UP; break;
        case 'R': current = KEY_R; break; case 'L': current = KEY_L; break;
        case '>': current = KEY_RIGHT; break; case '<': current = KEY_LEFT; break;
        case 'X': current = KEY_START; break; default: abort();
    }
}
unsigned keysDown(void) { return current; }
unsigned keysHeld(void) { return getenv("TEST_SELECT") ? KEY_SELECT : 0; }
const char *chainload_check(const char *filename, bool twl)
{
    (void)filename; (void)twl;
    return getenv("TEST_BAD_HEADER") ? "Bad header (test)" : NULL;
}
const char *chainload(const char *filename, const unsigned char *args, size_t length, bool twl)
{
    printf("<BOOT %s %s %zu>\n", twl ? "TWL" : "NTR", filename, length);
    for (size_t i = 0; i < length;) {
        printf("<ARG %s>\n", args + i); i += strlen((const char *)args + i) + 1;
    }
    exit(0);
}
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    events = getenv("TEST_KEYS"); if (!events) events = "";
    return shim_main(argc - 1, argv + 1);
}
