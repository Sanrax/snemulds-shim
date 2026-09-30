/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void edit(const char *input, const char *section, const char *const *keys,
                  const char *const *values, unsigned count, const char *expected)
{
    FILE *in = tmpfile(), *out = tmpfile(); assert(in && out);
    fputs(input, in); rewind(in);
    const char *error = shim_ini_edit(in, out, section, keys, values, count);
    assert((error == NULL) == (expected != NULL));
    if (!error) {
        char got[4096] = {0}; rewind(out);
        assert(fread(got, 1, sizeof(got) - 1, out) < sizeof(got) - 1);
        if (strcmp(got, expected)) { fprintf(stderr, "expected:\n%s\ngot:\n%s\n", expected, got); abort(); }
    }
    fclose(in); fclose(out);
}

static void write_text(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb"); assert(f); assert(fputs(s, f) >= 0); assert(!fclose(f));
}

static void check_text(const char *path, const char *s)
{
    char got[4096] = {0}; FILE *f = fopen(path, "rb"); assert(f);
    size_t n = fread(got, 1, sizeof(got) - 1, f); assert(!ferror(f)); fclose(f);
    assert(n == strlen(s)); assert(!strcmp(s, got));
}

int main(void)
{
    ShimConfig c; shim_config_defaults(&c);
    assert(!c.autoboot && !c.skip_macro_timer && c.default_mode == SHIM_AUTO);
    assert(!strcmp(c.legacy, "SNEmulDS_0.6a.nds"));
    FILE *invalid = tmpfile(); assert(invalid);
    fputs("[snemulds]\nskip_macro_timer=maybe\n", invalid); rewind(invalid);
    unsigned invalid_line;
    assert(shim_config_read(invalid, &c, &invalid_line) && invalid_line == 2);
    fclose(invalid);
    const char *keys[] = {"autoboot", "default"}, *values[] = {"true", "legacy"};
    const char *romkeys[] = {"ROMPath"}, *romvalues[] = {"/ROMs/SNES"};
    edit("", NULL, romkeys, romvalues, 1, "ROMPath = /ROMs/SNES\n");
    edit("ROMPath=/SNES/\nSound=1\n[GAME]\nSpeed=2\n", NULL, romkeys, romvalues, 1,
         "ROMPath = /ROMs/SNES\nSound=1\n[GAME]\nSpeed=2\n");
    edit("; keep comment\r\nROMPath = /old\r\n[Global]\r\nROMPath=/other\r\n", NULL, romkeys, romvalues, 1,
         "; keep comment\r\nROMPath = /ROMs/SNES\r\n[Global]\r\nROMPath=/other\r\n");
    edit("Sound=1\n[Global]\nROMPath=/other\n", NULL, romkeys, romvalues, 1,
         "Sound=1\nROMPath = /ROMs/SNES\n[Global]\nROMPath=/other\n");
    edit("\xEF\xBB\xBFROMPath=/old", NULL, romkeys, romvalues, 1,
         "ROMPath = /ROMs/SNES\n");
    edit("\xEF\xBB\xBF[Global]\nROMPath=/other\n", NULL, romkeys, romvalues, 1,
         "ROMPath = /ROMs/SNES\n[Global]\nROMPath=/other\n");
    edit("\xEF\xBB\xBF[snemulds]\n", "snemulds", keys, values, 2,
         "\xEF\xBB\xBF[snemulds]\nautoboot = true\ndefault = legacy\n");
    edit("; no newline", NULL, romkeys, romvalues, 1,
         "; no newline\nROMPath = /ROMs/SNES\n");
    edit("ROMPath=a\nrompath=b\n", NULL, romkeys, romvalues, 1, NULL);
    edit("[broken\n", NULL, romkeys, romvalues, 1, NULL);
    edit("[Global] unexpected\n", NULL, romkeys, romvalues, 1, NULL);
    FILE *binary = tmpfile(), *out = tmpfile(); assert(binary && out);
    assert(fwrite("ROMPath=x\0hidden\n", 1, 17, binary) == 17); rewind(binary);
    assert(shim_ini_edit(binary, out, NULL, romkeys, romvalues, 1));
    fclose(binary); fclose(out);
    edit("[snemulds]\nntr_path=../My Emulator.nds\n; preserve me\ntwl_path=e.srl\n",
         "snemulds", keys, values, 2,
         "[snemulds]\nntr_path=../My Emulator.nds\n; preserve me\ntwl_path=e.srl\nautoboot = true\ndefault = legacy\n");
    edit("autoboot=false\n[snemulds]\ndefault=auto\n[other]\nx=1\n",
         "snemulds", keys, values, 2,
         "autoboot = true\n[snemulds]\ndefault = legacy\n[other]\nx=1\n");
    edit("[other]\nx=1", "snemulds", keys, values, 2,
         "[other]\nx=1\n\n[snemulds]\nautoboot = true\ndefault = legacy\n");
    edit("[snemulds]\nautoboot=off\n[other]\nx=1\n", "snemulds", keys, values, 2,
         "[snemulds]\nautoboot = true\n[other]\nx=1\n\n[snemulds]\ndefault = legacy\n");
    for (unsigned dsi = 0; dsi < 2; ++dsi) {
        ShimMode modes[3]; unsigned n = shim_menu_modes(dsi, modes);
        assert(n == (dsi ? 3u : 2u));
        assert(modes[0] == SHIM_AUTO && modes[n - 1] == SHIM_LEGACY);
        for (unsigned m = 0; m < SHIM_MODE_COUNT; ++m) {
            assert(shim_target_twl(m, dsi) == (dsi && m == SHIM_AUTO));
            assert(!shim_storage_check(m, dsi, "fat:/"));
            assert((shim_storage_check(m, dsi, "sd:/") == NULL) == (dsi && m == SHIM_AUTO));
        }
    }
    char tmpdir[] = "/tmp/snemul-menu-XXXXXX"; assert(mkdtemp(tmpdir));
    char ini[256], cfg[256], collision[272];
    snprintf(ini, sizeof(ini), "%s/shim.ini", tmpdir);
    snprintf(cfg, sizeof(cfg), "%s/snemul.cfg", tmpdir);
    write_text(ini, "; custom paths\n[snemulds]\nntr_path=DS.nds\ntwl_path=DSi.srl\nlegacy_path=Old.nds\n");
    for (unsigned enabled = 0; enabled < 2; ++enabled)
    for (unsigned skip = 0; skip < 2; ++skip)
    for (unsigned mode = 0; mode < 3; ++mode) {
        c.autoboot = enabled; c.skip_macro_timer = skip; c.default_mode = mode;
        assert(!shim_save_preferences(ini, &c));
        FILE *f = fopen(ini, "rb"); assert(f);
        ShimConfig back; shim_config_defaults(&back); unsigned line;
        assert(!shim_config_read(f, &back, &line)); fclose(f);
        assert(back.autoboot == c.autoboot && back.default_mode == c.default_mode &&
               back.skip_macro_timer == c.skip_macro_timer);
        assert(!strcmp(back.ntr, "DS.nds") && !strcmp(back.twl, "DSi.srl") && !strcmp(back.legacy, "Old.nds"));
    }
    write_text(cfg, "# original\nROMPath=/SNES\nSound=1\n[Global]\nROMPath=/leave-alone\n");
    assert(!shim_legacy_config(cfg, "fat:/ROMs/SNES/Super Mario World.sfc"));
    check_text(cfg, "# original\nROMPath = /ROMs/SNES\nSound=1\n[Global]\nROMPath=/leave-alone\n");
    assert(!shim_legacy_config(cfg, "fat:/Homebrew.smc"));
    check_text(cfg, "# original\nROMPath = /\nSound=1\n[Global]\nROMPath=/leave-alone\n");
    snprintf(collision, sizeof(collision), "%s.shim-old", cfg);
    write_text(collision, "user recovery backup");
    assert(shim_legacy_config(cfg, "fat:/Other/Game.sfc"));
    check_text(collision, "user recovery backup");
    check_text(cfg, "# original\nROMPath = /\nSound=1\n[Global]\nROMPath=/leave-alone\n");
    assert(!remove(collision));
    write_text(cfg, "ROMPath=x\nROMPath=y\n");
    assert(shim_legacy_config(cfg, "fat:/Other/Game.sfc"));
    check_text(cfg, "ROMPath=x\nROMPath=y\n");
    assert(!remove(cfg));
    assert(!shim_legacy_config(cfg, "fat:/Folder with spaces/Game.sfc"));
    check_text(cfg, "ROMPath = /Folder with spaces\n");
    assert(!remove(cfg)); assert(!remove(ini)); assert(!rmdir(tmpdir));
    puts("PASS: launch-mode matrix, config migration, autoboot roundtrips, 0.6a ROMPath, preservation and failed-save safety");
}
