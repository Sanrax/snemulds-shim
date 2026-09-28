/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>

/* Called on ARM7, after reset, only for a TWL launch. TGDS SoundPowerON
 * configures the NTR mixer but leaves SNDEXTCNT inherited. Explicitly enable
 * and unmute I2S and select 100% NTR mixer / 0% DSP. Preserve the frequency
 * bit: changing it without matching codec PLL/dividers changes the clock.
 * No codec gain, calibration, or nonvolatile settings are changed here.
 */
void shim_twl_audio_prepare(void)
{
    volatile uint16_t *sndextcnt = (volatile uint16_t *)0x04004700;
    *sndextcnt = (*sndextcnt & (uint16_t)~0x400Fu) | 0x8008u;
}
