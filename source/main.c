/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <nds.h>
#include <nds/arm9/dldi.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "shim.h"
#include "config.h"
#include "ui.h"

typedef struct {
    bool dsi, dspico, configured;
    const char *device;
    char caller[SHIM_PATH_CAP], dir[SHIM_PATH_CAP], rom[SHIM_PATH_CAP], ini[256];
    ShimConfig config;
} App;

enum { INFO_VERSIONS, INFO_OVERVIEW, INFO_PATHS, INFO_ARGUMENTS, INFO_PAGE_COUNT };

static void message(const char *text, bool fatal)
{
    ui_begin("SNEmulDS Launcher");
    ui_section(3, "UNABLE TO LAUNCH");
    ui_wrap(5, 13, UI_ERROR, text);
    ui_footer(fatal ? "START  Exit" : "B  Back to menu   START  Exit", "");
    for (;;) {
        swiWaitForVBlank(); scanKeys();
        if (keysDown() & KEY_START) exit(0);
        if (!fatal && (keysDown() & KEY_B)) return;
    }
}

static bool file_ok(const char *path)
{
    struct stat s;
    return !stat(path, &s) && S_ISREG(s.st_mode) && s.st_size;
}

static const char *mode_name(ShimMode mode)
{
    return mode == SHIM_AUTO ? "0.6d Auto" : mode == SHIM_NTR ? "0.6d NTR" : "0.6a NTR";
}

static const char *target_path(const App *app, ShimMode mode, char target[SHIM_PATH_CAP])
{
    const char *configured = mode == SHIM_LEGACY ? app->config.legacy :
        shim_target_twl(mode, app->dsi) ? app->config.twl : app->config.ntr;
    if (!shim_path(configured, app->dir, target)) return "Invalid configured emulator path.";
    if (strncmp(target, app->device, strlen(app->device)))
        return "Emulator must be on the same\ncard as the shim and ROM.";
    return NULL;
}

static const char *arguments(const App *app, const char *target,
                             char translated[3][SHIM_PATH_CAP],
                             unsigned char wire[SHIM_WIRE_CAP], size_t *length)
{
    if (!shim_tgds_path(app->caller, translated[0]) ||
        !shim_tgds_path(target, translated[1]) ||
        (*app->rom && !shim_tgds_path(app->rom, translated[2])))
        return "Arguments exceed TGDS path limit.";
    unsigned count = *app->rom ? 3 : 2;
    *length = 0;
    for (unsigned i = 0; i < count; ++i) {
        size_t n = strlen(translated[i]) + 1;
        if (n > SHIM_WIRE_CAP - *length)
            return "Arguments exceed TGDS limits.\nShorten shim/emulator/ROM paths.";
        memcpy(wire + *length, translated[i], n);
        *length += n;
    }
    return NULL;
}

static const char *launch(const App *app, ShimMode mode)
{
    const char *error = shim_storage_check(mode, app->dsi, app->device);
    if (error) return error;
    char target[SHIM_PATH_CAP], translated[3][SHIM_PATH_CAP];
    error = target_path(app, mode, target);
    if (error) return error;
    if (!file_ok(target)) return "Emulator missing or unreadable.\nCheck its path on the R info\npage and in snemulds-shim.ini.";
    const bool twl = shim_target_twl(mode, app->dsi);
    error = chainload_check(target, twl, mode != SHIM_LEGACY);
    if (error) return error;
    unsigned char wire[SHIM_WIRE_CAP];
    size_t length = 0;
    if (mode == SHIM_LEGACY) {
        /* 0.6a deliberately gets no argv. Only it changes snemul.cfg. */
        if (*app->rom) {
            char cfg[32];
            snprintf(cfg, sizeof(cfg), "%ssnemul.cfg", app->device);
            error = shim_legacy_config(cfg, app->rom);
            if (error) return error;
        }
    } else {
        error = arguments(app, target, translated, wire, &length);
        if (error) return error;
    }
    ui_begin("SNEmulDS Launcher");
    ui_section(3, "LAUNCHING");
    ui_text(5, 1, UI_TEXT, mode_name(mode));
    if (mode == SHIM_LEGACY && *app->rom)
        ui_wrap(8, 3, UI_MUTED, "ROM folder set.\nSelect the game inside 0.6a.");
    return chainload(target, wire, length, twl, mode != SHIM_LEGACY);
}

static void menu(const App *app, const ShimMode *modes, unsigned count, unsigned row,
                 const char *status)
{
    ui_begin("SNEmulDS Launcher");
    const char *name = strrchr(app->rom, '/');
    ui_text(2, 1, UI_MUTED, "GAME");
    ui_wrap(3, 2, UI_TEXT, name ? name + 1 : "Choose a game in the emulator");
    ui_section(5, "PLAY");
    for (unsigned i = 0; i < count; ++i) {
        char label[32];
        const char *title = modes[i] == SHIM_LEGACY ? "0.6a   Classic" :
            modes[i] == SHIM_NTR ? "0.6d   NTR" : app->dsi ? "0.6d   Auto" : "0.6d   DS mode";
        snprintf(label, sizeof(label), "%c %s", i == row ? '>' : ' ', title);
        ui_bar(6 + i * 2, i == row ? UI_SELECTED : UI_TEXT, label);
        const char *detail = modes[i] == SHIM_LEGACY ? "  Pick game in emulator" :
            modes[i] == SHIM_NTR ? "  DS mode (NTR)" :
            app->dsi ? "  DSi mode (TWL)" : "  DS mode (NTR)";
        ui_bar(7 + i * 2, i == row ? UI_SELECTED : UI_MUTED, detail);
    }
    ui_section(13, "BOOT OPTIONS");
    char setting[64];
    snprintf(setting, sizeof(setting), "%c Autoboot   %s", row == count ? '>' : ' ', app->config.autoboot ? "On" : "Off");
    ui_bar(14, row == count ? UI_SELECTED : UI_TEXT, setting);
    snprintf(setting, sizeof(setting), "%c Default    %s", row == count + 1 ? '>' : ' ', mode_name(app->config.default_mode));
    ui_bar(15, row == count + 1 ? UI_SELECTED : UI_TEXT, setting);
    ui_text(17, 1, UI_GOOD, status);
    if (app->config.autoboot) ui_text(18, 1, UI_MUTED, "Hold SELECT to show this menu");
    ui_text(19, 1, UI_MUTED, "------------------------------");
    ui_text(20, 1, UI_TEXT, row < count ? "UP/DOWN  Move       A  Launch" : "UP/DOWN  Move");
    if (row >= count) ui_text(21, 1, UI_TEXT, "A/LEFT/RIGHT  Change");
    ui_text(22, 1, UI_MUTED, "R  Info            START  Exit");
}

static void info(const App *app, ShimMode mode, unsigned page, unsigned scroll)
{
    if (page == INFO_VERSIONS) { ui_versions(scroll); return; }
    static const char *const titles[] = {"Overview                  2/4", "Paths                     3/4", "Arguments                 4/4"};
    ui_begin(titles[page - INFO_OVERVIEW]);
    char target[SHIM_PATH_CAP];
    const char *error = target_path(app, mode, target);
    if (page == INFO_OVERVIEW) {
        ui_text(2, 1, UI_MUTED, "Launcher " SHIM_VERSION);
        ui_section(4, "SYSTEM");
        ui_text(5, 1, UI_TEXT, app->dsi ? "Running: DSi mode (TWL)" : "Running: DS mode (NTR)");
        ui_text(6, 1, UI_TEXT, app->dspico ? "Storage: DSpico SD" :
                !strcmp(app->device, "sd:/") ? "Storage: Console SD" : "Storage: Flashcart SD");
        ui_section(8, "SELECTED EMULATOR");
        ui_text(9, 1, UI_TEXT, mode == SHIM_LEGACY ? "SNEmulDS 0.6a" : "SNEmulDS 0.6d");
        ui_text(10, 1, UI_TEXT, mode == SHIM_AUTO ?
            (app->dsi ? "Auto -> DSi mode (TWL)" : "Auto -> DS mode (NTR)") : "DS mode (NTR)");
        if (mode != SHIM_LEGACY)
            ui_text(11, 1, UI_GOOD, "CFG game + language fixes");
        if (shim_target_twl(mode, app->dsi))
            ui_text(12, 1, UI_GOOD, "TWL touchscreen + audio fixes");
        ui_section(14, "AUTOBOOT");
        ui_text(15, 1, UI_TEXT, app->config.autoboot ? "Enabled: Yes" : "Enabled: No");
        char setting[48]; snprintf(setting, sizeof(setting), "Default: %s", mode_name(app->config.default_mode));
        ui_text(16, 1, UI_TEXT, setting);
    } else if (page == INFO_PATHS) {
        ui_section(2, "CONFIGURATION");
        ui_wrap(3, 5, UI_TEXT, app->ini);
        ui_text(8, 1, UI_MUTED, app->configured ? "Configuration loaded" : "Using beside-shim defaults");
        ui_section(10, "EMULATOR");
        ui_wrap(11, 5, error ? UI_ERROR : UI_TEXT, error ? error : target);
        ui_text(17, 1, UI_MUTED, shim_target_twl(mode, app->dsi) ? "Launch mode: DSi / TWL" : "Launch mode: DS / NTR");
    } else if (mode == SHIM_LEGACY) {
        ui_section(2, "ROM FOLDER");
        char dir[SHIM_PATH_CAP];
        if (*app->rom && shim_directory(app->rom, dir)) {
            char *p = strchr(dir, ':') + 1;
            size_t n = strlen(p); if (n > 1) p[n - 1] = 0;
            ui_wrap(3, 5, UI_TEXT, p);
        } else ui_wrap(3, 2, UI_MUTED, "No ROM supplied.\nFolder left unchanged.");
        ui_section(9, "CONFIG FILE");
        char cfg[32]; snprintf(cfg, sizeof(cfg), "%ssnemul.cfg", app->device);
        ui_text(10, 1, UI_TEXT, cfg);
        ui_text(13, 1, UI_MUTED, "0.6a uses no ROM arguments.");
        ui_text(15, 1, UI_TEXT, "Select your game inside 0.6a.");
    } else {
        char translated[3][SHIM_PATH_CAP]; unsigned char wire[SHIM_WIRE_CAP]; size_t length;
        if (!error) error = arguments(app, target, translated, wire, &length);
        if (error) ui_wrap(3, 8, UI_ERROR, error);
        else {
            static const char *const labels[] = {"CALLER / argv[0]", "EMULATOR / argv[1]", "ROM / argv[2]"};
            for (unsigned i = 0; i < (*app->rom ? 3u : 2u); ++i) {
                ui_section(2 + i * 6, labels[i]);
                ui_wrap(3 + i * 6, 5, UI_TEXT, translated[i]);
            }
            if (!*app->rom) ui_text(16, 1, UI_MUTED, "No ROM argument supplied.");
        }
    }
    ui_footer("L  Menu            R  Next", "UP/DOWN  Change info page");
}

int main(int argc, char **argv)
{
    ui_init();
    App app = {0};
    app.dsi = isDSiMode();
    scanKeys();
    bool bypass = (keysHeld() & KEY_SELECT) != 0;
    if (argc < 1 || argc > 2 || !argv || !argv[0] || (argc == 2 && !argv[1]))
        message("Launch normally, or pass one\n.sfc/.smc ROM argument.", true);
    if (!fatInitDefault()) message("Could not mount the SD card.", true);
    char cwd[512];
    if (!getcwd(cwd, sizeof(cwd))) message("Cannot determine current folder.", true);
    if (!shim_path(argv[0], cwd, app.caller) || !shim_directory(app.caller, app.dir))
        message("Invalid shim path in argv[0].", true);
    app.device = shim_device(app.caller);
    if (!app.device) message("Cannot identify shim storage.", true);
    app.dspico = app.dsi && !strcmp(app.device, "fat:/") && dldiIsValid(io_dldi_data) &&
                 io_dldi_data->ioInterface.ioType == 0x4F434950u;
    if (argc == 2) {
        if (!shim_path(argv[1], cwd, app.rom) || !shim_is_rom(app.rom))
            message("Expected a valid .sfc/.smc path.", true);
        if (strncmp(app.rom, app.device, strlen(app.device)))
            message("ROM and shim must be on\nthe same storage device.", true);
        if (!file_ok(app.rom)) message("ROM missing or unreadable.", true);
    }
    snprintf(app.ini, sizeof(app.ini), "%s%s", app.dir, SHIM_CONFIG_NAME);
    shim_config_defaults(&app.config);
    FILE *f = fopen(app.ini, "rb");
    app.configured = f != NULL;
    if (f) {
        unsigned line;
        const char *error = shim_config_read(f, &app.config, &line);
        fclose(f);
        if (error) {
            char text[256]; snprintf(text, sizeof(text), "INI line %u:\n%s", line, error);
            message(text, true);
        }
    } else if (errno != ENOENT) message("Cannot open shim configuration.", true);
    /* Sample again after mounting, so holding SELECT through launch is reliable. */
    for (unsigned i = 0; i < 2; ++i) {
        swiWaitForVBlank(); scanKeys(); bypass |= (keysHeld() & KEY_SELECT) != 0;
    }
    if (app.config.autoboot && !bypass) message(launch(&app, app.config.default_mode), false);
    ShimMode modes[SHIM_MODE_COUNT];
    unsigned count = shim_menu_modes(app.dsi, modes), row = 0, page = 0, scroll = 0;
    for (unsigned i = 0; i < count; ++i) if (modes[i] == app.config.default_mode) row = i;
    bool showing_info = false, redraw = true;
    const char *status = bypass ? "Autoboot bypassed with SELECT" : "";
    for (;;) {
        if (redraw) {
            if (showing_info) info(&app, row < count ? modes[row] : app.config.default_mode, page, scroll);
            else menu(&app, modes, count, row, status);
            redraw = false;
        }
        swiWaitForVBlank(); scanKeys(); unsigned keys = keysDown();
        if (keys & KEY_START) return 0;
        if (showing_info) {
            if (keys & KEY_L) { showing_info = false; redraw = true; }
            else if (page == INFO_VERSIONS && (keys & (KEY_UP | KEY_DOWN))) {
                if ((keys & KEY_UP) && scroll) { --scroll; redraw = true; }
                else if ((keys & KEY_DOWN) && scroll < ui_versions_scroll_limit()) {
                    ++scroll; redraw = true;
                }
            }
            else if (keys & (KEY_R | KEY_UP | KEY_DOWN)) {
                page = (page + ((keys & KEY_UP) ? INFO_PAGE_COUNT - 1 : 1)) % INFO_PAGE_COUNT;
                scroll = 0; redraw = true;
            }
            continue;
        }
        if (keys & KEY_R) { showing_info = true; page = 0; scroll = 0; redraw = true; continue; }
        if (keys & KEY_UP) { row = row ? row - 1 : count + 1; redraw = true; }
        else if (keys & KEY_DOWN) { row = (row + 1) % (count + 2); redraw = true; }
        else if (row < count && (keys & KEY_A)) { message(launch(&app, modes[row]), false); redraw = true; }
        else if (row >= count && (keys & (KEY_A | KEY_LEFT | KEY_RIGHT))) {
            ShimConfig next = app.config;
            if (row == count) next.autoboot = !next.autoboot;
            else next.default_mode = (ShimMode)((next.default_mode + ((keys & KEY_LEFT) ? 2 : 1)) % SHIM_MODE_COUNT);
            const char *error = shim_save_preferences(app.ini, &next);
            if (error) message(error, false);
            else { app.config = next; app.configured = true; status = "Settings saved"; }
            redraw = true;
        }
    }
}
