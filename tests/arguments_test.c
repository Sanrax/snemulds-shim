/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void path(const char *in, const char *cwd, const char *expected)
{
    char out[SHIM_PATH_CAP];
    bool ok = shim_path(in, cwd, out);
    assert(ok == (expected != NULL));
    if (ok) assert(!strcmp(out, expected));
}
static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(unsigned char *p, uint32_t n)
{
    p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24;
}

int main(int argc, char **argv)
{
    path("fat:/roms/My Game.sfc", NULL, "fat:/roms/My Game.sfc");
    path("0:/roms/Game.smc", NULL, "fat:/roms/Game.smc");
    path("/roms/Game.sfc", "sd:/irrelevant", "sd:/roms/Game.sfc");
    path("Game.sfc", "fat:/roms", "fat:/roms/Game.sfc");
    path("../Game.sfc", "fat:/roms/sub", "fat:/roms/Game.sfc");
    path("/a//b/.././Game.sfc", NULL, "fat:/a/Game.sfc");
    path("../../Game.sfc", "fat:/a", NULL);
    path("sd:/roms/Game.sfc", NULL, "sd:/roms/Game.sfc");
    path("fat1:/roms/Game.sfc", NULL, NULL);
    path("Game.sfc", "sd:/roms", "sd:/roms/Game.sfc");
    path("", "fat:/", NULL);
    path("/", "fat:/", NULL);
    path("fat:/a\\b.sfc", NULL, NULL);
    path("fat:/a\nb.sfc", NULL, NULL);
    path("../Game.sfc", "sd:/roms/sub", "sd:/roms/Game.sfc");
    path("fat:/Game.sfc", "sd:/", "fat:/Game.sfc");
    path("0:/Game.sfc", "sd:/", "sd:/Game.sfc");
    path("../../Game.sfc", "sd:/a", NULL);
    assert(!strcmp(shim_device("fat:/apps/shim.nds"), "fat:/"));
    assert(!strcmp(shim_device("sd:/apps/shim.nds"), "sd:/"));
    assert(!shim_device("0:/apps/shim.nds"));
    char longpath[130];
    memset(longpath, 'a', sizeof(longpath));
    memcpy(longpath, "fat:/", 5);
    longpath[126] = 0;
    char out[SHIM_PATH_CAP];
    assert(shim_path(longpath, NULL, out));
    longpath[126] = 'a'; longpath[127] = 0;
    assert(!shim_path(longpath, NULL, out));
    assert(shim_is_rom("fat:/My Homebrew.SFC"));
    assert(shim_is_rom("fat:/My Homebrew.sMc"));
    assert(!shim_is_rom("fat:/My Homebrew.sfc.zip"));

    const char *args[3] = {"fat:/ToolchainGenericDS-multiboot.nds", "fat:/SNEmulDS.nds", "fat:/roms/My Game.sfc"};
    unsigned char wire[SHIM_WIRE_CAP];
    size_t n = shim_pack(args, wire);
    assert(n == strlen(args[0]) + strlen(args[1]) + strlen(args[2]) + 3);
    /* Independent receiver model: split on NUL, then TGDS fat:/ -> 0:/. */
    unsigned count = 0;
    char received[3][SHIM_PATH_CAP] = {{0}};
    for (size_t start = 0, i = 0; i < n; ++i) {
        if (wire[i]) continue;
        assert(count < 3 && i - start >= 5);
        strcpy(received[count], "0:/");
        memcpy(received[count] + 3, wire + start + 5, i - start - 5);
        ++count;
        start = i + 1;
    }
    assert(count == 3);
    assert(!strcmp(received[1], "0:/SNEmulDS.nds"));
    assert(!strcmp(received[2], "0:/roms/My Game.sfc"));
    args[0] = ""; assert(!shim_pack(args, wire));
    longpath[126] = 0;
    args[0] = args[1] = args[2] = longpath;
    assert(!shim_pack(args, wire));

    /* DLDI fixture: relocation affects pointers, preserves scalars and clears BSS. */
    unsigned char dldi[256] = {0};
    uint32_t old = 0x02300000, target = 0x06001000;
    dldi[13] = dldi[15] = 8; dldi[14] = 15;
    put32(dldi + 0x40, old); put32(dldi + 0x44, old + 192);
    put32(dldi + 0x48, old + 128); put32(dldi + 0x4c, old + 192);
    put32(dldi + 0x50, old + 128); put32(dldi + 0x54, old + 192);
    put32(dldi + 0x58, old + 192); put32(dldi + 0x5c, old + 208);
    for (unsigned i = 0x68; i < 0x80; i += 4) put32(dldi + i, old + 129);
    put32(dldi + 0x80, old + 140);
    put32(dldi + 0x84, 0x040001A0);
    memset(dldi + 192, 0xA5, 16);
    assert(shim_dldi_relocate(dldi, sizeof(dldi), target));
    assert(u32(dldi + 0x40) == target);
    assert(u32(dldi + 0x68) == target + 129);
    assert(u32(dldi + 0x80) == target + 140); /* Not relocated three times */
    assert(u32(dldi + 0x84) == 0x040001A0);
    for (unsigned i = 192; i < 208; ++i) assert(!dldi[i]);
    put32(dldi + 0x5c, target + 260);
    assert(!shim_dldi_relocate(dldi, sizeof(dldi), old));
    /* Configuration beside argv[0], independently of a ROM working folder. */
    char caller[SHIM_PATH_CAP], dir[SHIM_PATH_CAP], native[SHIM_PATH_CAP];
    assert(shim_path("sd:/apps/snes/shim.nds", "sd:/roms", caller));
    assert(shim_directory(caller, dir));
    assert(!strcmp(dir, "sd:/apps/snes/"));
    assert(shim_path("../emulators/SNEmulDS.srl", dir, native));
    assert(!strcmp(native, "sd:/apps/emulators/SNEmulDS.srl"));
    assert(shim_tgds_path(native, out));
    assert(!strcmp(out, "fat:/apps/emulators/SNEmulDS.srl"));
    assert(shim_directory("fat:/shim.nds", dir));
    assert(!strcmp(dir, "fat:/"));

    const char *inis[] = {
        "\xEF\xBB\xBF[snemulds]\r\nntr_path = \"../DS/My Emulator.nds\" ; note\r\nTWL_PATH=../DSi/SNEmulDS.srl\r\n",
        "[snemulds]\nntr_path=\n",
        "[snemulds]\ntwl_path=\"unclosed\n",
        "[snemulds]\nntr_path=a.nds\nntr_path=b.nds\n",
        "[snemulds]\nwrong_path=a.nds\n",
        "[snemulds]\ntwl_path=\"x.srl\" garbage\n",
        "[snemulds]\ntwl_path=C:\\x.srl\n",
        "[other]\nignored = a\n[snemulds]\nntr_path=a #1;2.nds",
        "# comment only\n",
    };
    for (unsigned i = 0; i < sizeof(inis) / sizeof(inis[0]); ++i) {
        FILE *f = tmpfile(); assert(f);
        fputs(inis[i], f); rewind(f);
        ShimConfig c; shim_config_defaults(&c); unsigned line;
        const char *error = shim_config_read(f, &c, &line); fclose(f);
        assert((error == NULL) == (i == 0 || i >= 7));
        if (i == 0) {
            assert(!strcmp(c.ntr, "../DS/My Emulator.nds"));
            assert(!strcmp(c.twl, "../DSi/SNEmulDS.srl"));
        }
        if (i == 7) assert(!strcmp(c.ntr, "a #1;2.nds"));
        if (i == 8) {
            assert(!strcmp(c.ntr, "SNEmulDS.nds"));
            assert(!strcmp(c.twl, "SNEmulDS.srl"));
        }
    }
    FILE *f = tmpfile(); assert(f);
    fputs("ntr_path=", f);
    for (unsigned i = 0; i < 520; ++i) fputc('a', f);
    rewind(f); ShimConfig c; unsigned line;
    assert(shim_config_read(f, &c, &line)); fclose(f);

    /* Binary-layout fixtures: reject wrong modes, truncation, and overlap. */
    unsigned char h[512] = {0};
    put32(h + 0x20, 0x1000); put32(h + 0x24, 0x02000800);
    put32(h + 0x28, 0x02000000); put32(h + 0x2c, 0x60000);
    put32(h + 0x30, 0x61000); put32(h + 0x34, 0x02380000);
    put32(h + 0x38, 0x02380000); put32(h + 0x3c, 0x10000);
    assert(!shim_check_header(h, sizeof(h), 0x71000, false));
    assert(shim_check_header(h, sizeof(h), 0x71000, true));
    assert(shim_check_header(h, sizeof(h), 0x70000, false));
    h[0x12] = 2;
    put32(h + 0x1c0, 0x70000); put32(h + 0x1c8, 0x02400000); put32(h + 0x1cc, 0x1000);
    put32(h + 0x1d0, 0x71000); put32(h + 0x1d8, 0x02e80000); put32(h + 0x1dc, 0x1000);
    assert(!shim_check_header(h, sizeof(h), 0x72000, true));
    assert(shim_check_header(h, sizeof(h), 0x72000, false));
    put32(h + 0x1d8, 0x02400000);
    assert(shim_check_header(h, sizeof(h), 0x72000, true));
    put32(h + 0x1d8, 0x02fff000);
    assert(shim_check_header(h, sizeof(h), 0x72000, true));
    put32(h + 0x1d8, 0x027ff000);
    assert(shim_check_header(h, sizeof(h), 0x72000, true));

    /* Narrow TGDS 1.65 force-DLDI signature: unique match or no patch. */
    unsigned char tgds[80] = {0};
    const uint32_t sequence[] = {
        0xE5933000, 0xE3530003, 0x0A00000B, 0xEB0006B6, 0xE1A03000,
        0xE2233001, 0xE20330FF, 0xE3530000, 0x1A000005
    };
    for (unsigned i = 0; i < sizeof(sequence) / sizeof(sequence[0]); ++i)
        put32(tgds + 12 + i * 4, sequence[i]);
    assert(shim_tgds_dldi_patch_offset(tgds, sizeof(tgds)) == 20);
    put32(tgds + 20, 0xEA00000B);
    assert(shim_tgds_dldi_patch_offset(tgds, sizeof(tgds)) == SIZE_MAX);

    /* Optional real upstream binaries supplied by the build verifier. */
    for (int i = 1; i < argc; ++i) {
        f = fopen(argv[i], "rb"); assert(f);
        assert(fread(h, 1, sizeof(h), f) == sizeof(h));
        assert(!fseek(f, 0, SEEK_END)); long size = ftell(f);
        assert(!shim_check_header(h, sizeof(h), size, h[0x12] == 2));
        assert(shim_check_header(h, sizeof(h), size, h[0x12] != 2));
        if (h[0x12] == 2) {
            uint32_t offset = u32(h + 0x30), arm7_size = u32(h + 0x3c);
            unsigned char *arm7 = malloc(arm7_size); assert(arm7);
            assert(!fseek(f, offset, SEEK_SET));
            assert(fread(arm7, 1, arm7_size, f) == arm7_size);
            assert(shim_tgds_dldi_patch_offset(arm7, arm7_size) == 0x6514);
            free(arm7);
        }
        fclose(f);
    }
    puts("PASS: device/config paths, NTR/TWL headers, TGDS argv, DLDI relocation and TWL force-DLDI signature");
}
