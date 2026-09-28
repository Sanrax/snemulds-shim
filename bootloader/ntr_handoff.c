/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Separate DSi -> NTR handoff. This is never called for a TWL target.
 * Codec register sequence: Pico-loader, Copyright (c) 2025 LNH team, Zlib.
 * See third_party/pico-reference for original source and license.
 * SPI/I2C transactions follow the libnds hardware protocols (Zlib).
 */
#include <stdbool.h>
#include <stdint.h>
#include <nds/arm7/serial.h>
#include "ntr_codec_generated.h"

static bool spi_idle(void)
{
    for (unsigned n = 65536; n; --n) if (!(REG_SPICNT & SPI_BUSY)) return true;
    return false;
}

static bool spi(uint16_t control, uint8_t data, uint8_t *out)
{
    if (!spi_idle()) return false;
    REG_SPICNT = control; REG_SPIDATA = data;
    if (!spi_idle()) return false;
    if (out) *out = REG_SPIDATA;
    return true;
}

static bool codec(uint8_t reg, uint8_t value, bool read)
{
    return spi(SPI_ENABLE | SPI_TARGET_CODEC | SPI_CONTINUOUS,
               (reg << 1) | (read ? 1 : 0), 0) &&
           spi(SPI_ENABLE | SPI_TARGET_CODEC, value, 0);
}

static bool i2c_idle(void)
{
    for (unsigned n = 65536; n; --n)
        if (!(*(volatile uint8_t*)0x04004501 & 0x80)) return true;
    return false;
}

static bool i2c_send(uint8_t data, uint8_t control)
{
    if (!i2c_idle()) return false;
    *(volatile uint8_t*)0x04004500 = data;
    *(volatile uint8_t*)0x04004501 = control;
    if (!i2c_idle()) return false;
    bool ack = (*(volatile uint8_t*)0x04004501 & 0x10) != 0;
    for (volatile unsigned n = 0; n < 0x600; ++n) { }
    return ack;
}

static bool mcu_ntr_mode(void)
{
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        bool ok = i2c_send(0x4A, 0xC2) && i2c_send(0x12, 0xC0) && i2c_send(0, 0xC0);
        if (!i2c_idle()) return false;
        *(volatile uint8_t*)0x04004501 = 0xC5;
        if (!i2c_idle()) return false;
        if (ok) return true;
    }
    return false;
}

bool shim_ntr_prepare(void)
{
    /* Match Pico's 32.73 kHz setup before the complete codec mode sequence. */
    *(volatile uint16_t*)0x04004700 = 0x0008;
    if (!codec(0, 0, false) || !codec(11, 0x87, false) ||
        !codec(18, 0x87, false) || !codec(6, 21, false)) return false;
    *(volatile uint16_t*)0x04004700 = 0x8008;
    uint8_t page = 0;
    for (unsigned i = 0; i < sizeof(ntr_codec) / sizeof(ntr_codec[0]); ++i) {
        if (ntr_codec[i][0] != page) {
            if (!codec(page == 255 ? 127 : 0, ntr_codec[i][0], false)) return false;
            page = ntr_codec[i][0];
        }
        if (!codec(ntr_codec[i][1] & 0x7F, ntr_codec[i][2], (ntr_codec[i][1] & 0x80) != 0))
            return false;
    }
    uint8_t power;
    if (!spi(SPI_ENABLE | SPI_DEVICE_POWER | SPI_CONTINUOUS, 0x80, 0) ||
        !spi(SPI_ENABLE | SPI_DEVICE_POWER, 0, &power) ||
        !spi(SPI_ENABLE | SPI_DEVICE_POWER | SPI_CONTINUOUS, 0, 0) ||
        !spi(SPI_ENABLE | SPI_DEVICE_POWER, (power | 1) & ~2, 0)) return false;
    REG_SPICNT = 0;
    *(volatile uint16_t*)0x04000500 = 0x807F;
    if (!mcu_ntr_mode()) return false;
    *(volatile uint16_t*)0x04004C04 |= 1u << 8;
    *(volatile uint16_t*)0x04004000 = 0x0703; /* NTR ARM9 and ARM7 BIOS */
    *(volatile uint32_t*)0x03FFFFC4 = 0;
    *(volatile uint32_t*)0x03FFFFC8 = 0;
    return true;
}
