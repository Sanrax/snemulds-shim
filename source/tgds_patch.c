/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim.h"
#include "tgds_language.h"
#include "tgds_settings.h"

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void write32(unsigned char *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (i * 8));
}

/*
 * TGDS 1.65's ARM7InitDLDI() selects internal SD when it is available.
 * For a TWL application loaded from a DSi-mode flashcart, replace only the
 * branch that enters that decision with an unconditional branch to TGDS's
 * existing DLDI initialization path. The surrounding O0 instruction sequence
 * makes this deliberately narrower than a version- or address-only patch.
 */
size_t shim_tgds_dldi_patch_offset(const void *data, size_t size)
{
    const unsigned char *p = data;
    size_t found = SIZE_MAX;
    unsigned matches = 0;
    if (!p) return SIZE_MAX;
    for (size_t i = 0; i + 36 <= size; i += 4) {
        if (read32(p + i) != 0xE5933000 ||       /* ldr r3, [r3] */
            read32(p + i + 4) != 0xE3530003 ||   /* cmp r3, #3 */
            read32(p + i + 8) != 0x0A00000B ||   /* beq DLDI path */
            (read32(p + i + 12) & 0xFF000000) != 0xEB000000 ||
            read32(p + i + 16) != 0xE1A03000 ||
            read32(p + i + 20) != 0xE2233001 ||
            read32(p + i + 24) != 0xE20330FF ||
            read32(p + i + 28) != 0xE3530000 ||
            read32(p + i + 32) != 0x1A000005)
            continue;
        found = i + 8;
        ++matches;
    }
    return matches == 1 ? found : SIZE_MAX;
}

static bool branch_target(size_t size, size_t site, uint32_t opcode, size_t *target)
{
    int32_t words = (int32_t)(opcode & 0x00FFFFFFu);
    if (words & 0x00800000) words -= 0x01000000;
    int64_t address = (int64_t)site + 8 + (int64_t)words * 4;
    if (address < 0 || address > (int64_t)size - 4) return false;
    *target = (size_t)address;
    return true;
}

/* TGDS 1.65 reads [GUI] Language before calling these two ARM-mode display
 * switches. Each reselects getLanguage() afterwards, discarding the config.
 * GUI_init() only changes the display/console and GUI_clear() only clears it;
 * neither changes GUI.string. Keep the initial firmware selection and the
 * explicit config override by skipping just the two later GUI_setLanguage calls.
 * The paired call targets and GUI_setLanguage body identify the stock code.
 */
bool shim_tgds_language_patch(void *arm9, size_t size, bool apply)
{
    unsigned char *p = arm9;
    size_t first = SIZE_MAX;
    unsigned matches = 0;
    if (!p) return false;
    for (size_t i = 0; i + 56 <= size; i += 4) {
        if (read32(p + i) != 0xE92D4008 ||       /* push {r3, lr} */
            read32(p + i + 4) != 0xE3A00000 ||   /* mov r0, #0 */
            read32(p + i + 20) != 0xE8BD4008 ||  /* pop {r3, lr} */
            read32(p + i + 28) != 0xE92D4008 ||
            read32(p + i + 32) != 0xE3A00001 ||  /* mov r0, #1 */
            read32(p + i + 48) != 0xE8BD4008)
            continue;
        size_t init0, init1, firmware0, firmware1, lang0, lang1, clear0, clear1;
        const size_t sites[] = {i + 8, i + 12, i + 16, i + 24,
                                i + 36, i + 40, i + 44, i + 52};
        size_t *targets[] = {&init0, &firmware0, &lang0, &clear0,
                             &init1, &firmware1, &lang1, &clear1};
        bool valid = true;
        for (unsigned n = 0; n < 8; ++n) {
            uint32_t instruction = read32(p + sites[n]);
            uint32_t expected = n == 3 || n == 7 ? 0xEA000000 : 0xEB000000;
            if ((instruction & 0xFF000000) != expected ||
                !branch_target(size, sites[n], instruction, targets[n])) {
                valid = false;
                break;
            }
        }
        if (!valid || init0 != init1 || firmware0 != firmware1 ||
            lang0 != lang1 || clear0 != clear1 ||
            lang0 + 0xA0 > size ||
            read32(p + lang0) != 0xE3500005 || /* cmp r0, #5 */
            read32(p + lang0 + 0x98) != 0xE5832028 || /* GUI.string = r2 */
            read32(p + lang0 + 0x9C) != 0xE12FFF1E)
            continue;
        first = i;
        ++matches;
    }
    if (matches != 1) return false;
    if (apply) {
        const uint32_t nop = 0xE1A00000; /* mov r0, r0 */
        for (unsigned n = 0; n < 4; ++n) {
            p[first + 16 + n] = (unsigned char)(nop >> (n * 8));
            p[first + 44 + n] = (unsigned char)(nop >> (n * 8));
        }
    }
    return true;
}

/* Both stock TGDS1.65 images contain the same ARM9 changeROM() sequence.
 * At 0x126ac it calls GUI_showROMInfos(size) after the ROM title is trimmed,
 * then reset_SNES() and loadSRAM(). The GUI save action writes settings under
 * that title, but the ROM load never calls readOptionsFromConfig(title).
 *
 * Redirect only the GUI call through 24 bytes of zero alignment padding before
 * the DLDI area. The veneer reads the saved game section, restores the size
 * argument and return address, then tail-calls the original GUI routine.
 * The title begins at SNES base + 0x1004; +0x1018 is title[20], its NUL.
 * Nothing is changed on disk; the original GUI save path remains in charge.
 */
bool shim_tgds_game_settings_patch(void *arm9, size_t size, bool apply)
{
    enum { CAVE = 0x0AF0, SITE = 0x126AC, TITLE = 0x03001004 };
    unsigned char *p = arm9;
    size_t target;
    if (!p || size < 0x13574) return false;
    for (size_t i = CAVE; i < CAVE + 24; ++i) if (p[i]) return false;
    uint32_t global = read32(p + 0x13570);
    if (global < 0x02000000 || global - 0x02000000 > size - 7 ||
        read32(p + 0x0AE0) != 0xE12FFF1E || /* end of CRT code */
        read32(p + 0x1800) != 0xBF8DA5ED || /* following DLDI area */
        p[global - 0x02000000] != 'G' ||
        p[global - 0x02000000 + 1] != 'l' ||
        p[global - 0x02000000 + 2] != 'o' ||
        p[global - 0x02000000 + 3] != 'b' ||
        p[global - 0x02000000 + 4] != 'a' ||
        p[global - 0x02000000 + 5] != 'l' ||
        p[global - 0x02000000 + 6] != 0 ||
        read32(p + 0x125DC) != 0xE5C32004 || /* title[i] = NUL */
        read32(p + 0x125FC) != 0xE5D33004 || /* title[i] in trim loop */
        read32(p + 0x12698) != 0xE59F3034 || /* SNES base */
        read32(p + 0x1269C) != 0xE2833A01 || /* +0x1000 */
        read32(p + 0x126A4) != 0xE5C32018 || /* title[20] = NUL */
        read32(p + 0x126A8) != 0xE51B001C || /* GUI size argument */
        read32(p + 0x126D4) != 0x03000000 ||
        read32(p + 0x126D8) != TITLE || /* title passed to strlen */
        read32(p + 0x12D94) != 0xE92D4070 || /* config reader */
        global != read32(p + 0x131F8) || /* same "Global" in config reader */
        !branch_target(size, SITE, read32(p + SITE), &target) ||
        target != 0x1E204 ||
        !branch_target(size, SITE + 4, read32(p + SITE + 4), &target) ||
        target != 0x1AD20 ||
        !branch_target(size, SITE + 8, read32(p + SITE + 8), &target) ||
        target != 0x12400 ||
        !branch_target(size, 0x13548, read32(p + 0x13548), &target) ||
        target != 0x12D94)
        return false;
    if ((read32(p + SITE) & 0xFF000000) != 0xEB000000 ||
        (read32(p + SITE + 4) & 0xFF000000) != 0xEB000000 ||
        (read32(p + SITE + 8) & 0xFF000000) != 0xEB000000 ||
        (read32(p + 0x13548) & 0xFF000000) != 0xEB000000)
        return false;
    if (apply) {
        write32(p + CAVE, 0xE92D4001);      /* push {r0, lr} */
        write32(p + CAVE + 4, 0xE59F0008);  /* ldr r0, [pc, #8] */
        write32(p + CAVE + 8, 0xEB000000 |
                ((0x12D94 - (CAVE + 16)) / 4)); /* readOptionsFromConfig */
        write32(p + CAVE + 12, 0xE8BD4001); /* pop {r0, lr} */
        write32(p + CAVE + 16, 0xEA000000 |
                ((0x1E204 - (CAVE + 24)) / 4)); /* GUI_showROMInfos */
        write32(p + CAVE + 20, TITLE);       /* SNES base + 0x1004 */
        write32(p + SITE, 0xEB000000 |
                (((CAVE - (SITE + 8)) / 4) & 0x00FFFFFF));
    }
    return true;
}
