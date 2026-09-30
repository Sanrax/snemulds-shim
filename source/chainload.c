/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <nds.h>
#include <nds/arm9/dldi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "load_bin.h"
#include "shim.h"
#include "tgds_io.h"
#include "tgds_language.h"

/* Header ABI of devkitPro/nds-bootloader at 69cea3c5. */
typedef struct {
    u32 branch, cluster, init_disc, patch_dldi, arg_offset, arg_size;
    u32 dldi_offset, dsi_sd, dsi_mode, force_tgds_dldi, dspico_arm9_io, force_ntr,
        patch_language;
} LoaderHeader;

static u32 read32(const unsigned char *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static const char *check_nds(const char *path, u32 *cluster, bool twl, bool force_tgds_dldi,
                             bool dspico_arm9_io, bool patch_language)
{
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 512 ||
        st.st_ino < 2 || st.st_ino >= 0x0FFFFFF0)
        return "Cannot get NDS file/FAT cluster.";
    unsigned char h[512];
    FILE *f = fopen(path, "rb");
    if (!f) return "Cannot open the NDS binary.";
    size_t n = fread(h, 1, sizeof(h), f);
    if (n != sizeof(h)) {
        fclose(f);
        return "Cannot read NDS header.";
    }
    const char *error = shim_check_header(h, sizeof(h), st.st_size, twl);
    if (error) {
        fclose(f);
        return error;
    }
    if (twl) {
        u32 offset = read32(h + 0x30), size = read32(h + 0x3C);
        unsigned char *arm7 = malloc(size);
        if (!arm7) {
            fclose(f);
            return "Not enough memory to inspect TWL binary.";
        }
        bool read_ok = !fseek(f, offset, SEEK_SET) && fread(arm7, 1, size, f) == size;
        size_t patch = read_ok ? shim_tgds_dldi_patch_offset(arm7, size) : SIZE_MAX;
        bool touch_ok = read_ok && read32(h + 0x38) == 0x02380000 &&
                        shim_tgds_twl_touch(arm7, size, false);
        free(arm7);
        if (!read_ok) {
            fclose(f);
            return "Cannot inspect the TWL ARM7 binary.";
        }
        if (force_tgds_dldi && patch == SIZE_MAX) {
            fclose(f);
            return "Unsupported TWL build for\nflashcart/DSpico storage.";
        }
        if (!touch_ok) {
            fclose(f);
            return "Unsupported TWL input code.\nUse stock SnemulDS 0.6d SRL.";
        }
    }
    if (dspico_arm9_io || patch_language) {
        if (read32(h + 0x28) != 0x02000000) {
            fclose(f);
            return "Unsupported 0.6d ARM9 load address.";
        }
        u32 offset = read32(h + 0x20), size = read32(h + 0x2C);
        unsigned char *arm9 = malloc(size);
        if (!arm9) {
            fclose(f);
            return "Not enough memory to inspect 0.6d ARM9.";
        }
        bool ok = !fseek(f, offset, SEEK_SET) && fread(arm9, 1, size, f) == size;
        if (!ok) {
            free(arm9);
            fclose(f);
            return "Cannot inspect the 0.6d ARM9 binary.";
        }
        bool language_ok = !patch_language || shim_tgds_language_patch(arm9, size, false);
        bool io_ok = !dspico_arm9_io || shim_tgds_dspico_io(arm9, size, false);
        free(arm9);
        if (!language_ok || !io_ok) {
            fclose(f);
            return !language_ok ? "Unsupported 0.6d language code.\nUse stock SNEmulDS 0.6d." :
                   "Unsupported TWL ARM9 build.\nUse stock SNEmulDS 0.6d SRL.";
        }
    }
    fclose(f);
    *cluster = st.st_ino;
    return NULL;
}

const char *chainload_check(const char *filename, bool twl, bool patch_language)
{
    const bool dsi = isDSiMode(), sd = !strncmp(filename, "sd:/", 4);
    if (twl && !dsi) return "A TWL binary cannot run in DS mode.";
    if (sd && !twl) return "NTR requires flashcart storage.";
    if (dsi && !(REG_SCFG_EXT & (1u << 31)))
        return "Launch needs unlocked SCFG.\nUse a compatible DSi launcher.";
    const bool force = twl && !sd;
    const bool pico = force && dldiIsValid(io_dldi_data) &&
                      io_dldi_data->ioInterface.ioType == 0x4F434950u;
    u32 cluster;
    return check_nds(filename, &cluster, twl, force, pico, patch_language);
}

const char *chainload(const char *filename, const unsigned char *args, size_t length,
                      bool twl, bool patch_language)
{
    u32 cluster;
    const bool dsi = isDSiMode();
    const bool sd = !strncmp(filename, "sd:/", 4);
    const bool force_tgds_dldi = twl && !sd;
    const bool dspico_arm9_io = force_tgds_dldi && dldiIsValid(io_dldi_data) &&
                               io_dldi_data->ioInterface.ioType == 0x4F434950u;
    if (twl && !dsi) return "A TWL binary cannot run in DS mode.";
    if (sd && !twl) return "NTR requires flashcart storage.";
    if (dsi && !(REG_SCFG_EXT & (1u << 31)))
        return "TGDS needs unlocked SCFG.\nUse a compatible DSi launcher.";
    const char *error = check_nds(filename, &cluster, twl, force_tgds_dldi,
                                  dspico_arm9_io, patch_language);
    if (error) return error;
    if (length > SHIM_WIRE_CAP || (length && !args)) return "Invalid argument length.";
    if (load_bin_size < sizeof(LoaderHeader)) return "Invalid embedded loader.";
    const LoaderHeader *base = (const LoaderHeader *)load_bin;
    u32 arg_offset = (base->arg_offset + 3) & ~3u;
    size_t image_size = (arg_offset + length + 3) & ~3u;
    if (arg_offset < load_bin_size || image_size > 0x1F000 ||
        base->dldi_offset > load_bin_size - sizeof(DLDI_INTERFACE))
        return "Embedded loader exceeds VRAM.";

    /* Prepare everything in ordinary RAM; VRAM does not support byte writes. */
    unsigned char *image = calloc(1, image_size);
    if (!image) return "Not enough memory for loader.";
    memcpy(image, load_bin, load_bin_size);
    LoaderHeader *hdr = (LoaderHeader *)image;
    DLDI_INTERFACE *area = (DLDI_INTERFACE *)(image + hdr->dldi_offset);
    if (!sd) {
        unsigned allocation = area->allocatedSize;
        const DLDI_INTERFACE *driver = io_dldi_data;
        if (!dldiIsValid(driver) || driver->ioInterface.ioType == 0x49444C44 ||
            driver->driverSize < 7 || driver->driverSize > 15 ||
            allocation > 15 || driver->driverSize > allocation ||
            hdr->dldi_offset + (1u << allocation) > load_bin_size) {
            free(image);
            return "Missing/unsupported DLDI driver.";
        }
        size_t driver_bytes = 1u << driver->driverSize;
        /* The driver was relocated by the launching menu; relocate its copy again. */
        memset(area, 0, 1u << allocation);
        memcpy(area, driver, driver_bytes);
        area->allocatedSize = allocation;
        /* BlocksDS dldiRelocate accesses the target address. Our bytes are still in
           a staging image, so use an offset-based relocator instead. */
        if (!shim_dldi_relocate(area, 1u << allocation, 0x06000000u + hdr->dldi_offset)) {
            free(image);
            return "Invalid DLDI relocation sections.";
        }
    }
    hdr->cluster = cluster;
    hdr->init_disc = 1;
    hdr->patch_dldi = !sd;
    hdr->dsi_sd = sd;
    hdr->dsi_mode = twl;
    hdr->force_ntr = dsi && !twl;
    hdr->force_tgds_dldi = force_tgds_dldi;
    hdr->dspico_arm9_io = dspico_arm9_io;
    hdr->patch_language = patch_language;
    hdr->arg_offset = arg_offset;
    hdr->arg_size = length;
    memcpy(image + arg_offset, args, length);

    VRAM_C_CR = VRAM_ENABLE | VRAM_C_LCD;
    vu32 *vram = (vu32 *)0x06840000;
    const u32 *src = (const u32 *)image;
    for (size_t i = 0; i < image_size / 4; ++i) vram[i] = src[i];
    free(image);
    /* All operations that can return an error finish before IRQ shutdown. */
    irqDisable(IRQ_ALL);
    if (dsi) {
        for (unsigned i = 0; i < 4; ++i)
            *(vu32 *)(0x0400411C + i * 0x1C) = 0;
    }
    VRAM_C_CR = VRAM_ENABLE | VRAM_C_ARM7_0x06000000;
    REG_EXMEMCNT |= ARM7_OWNS_ROM | ARM7_OWNS_CARD;
    *(vu32 *)0x02FFFFFC = 0;
    *(vu32 *)0x02FFFE04 = 0xE59FF018; /* ldr pc, [pc, #0x18] */
    *(vu32 *)0x02FFFE24 = 0x02FFFE04;
    DC_FlushAll();
    resetARM7(0x06000000);
    swiSoftReset();
    return "Unexpected return from reset.";
}
