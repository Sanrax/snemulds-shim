/* SPDX-License-Identifier: GPL-3.0-or-later
 * Replacement routines for the fingerprinted TGDS 1.65 TWL ARM7 image only.
 * No reserved RAM, boot mappings, audio registers or clocks are changed.
 */
#include <stdint.h>
#include <stdbool.h>
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
typedef struct { uint16_t rawx, rawy, px, py, z1, z2; } Touch;
extern bool cdcTouchRead(Touch *p);
extern void cdcTouchInit(void);
extern bool touchPenDown(void);

static int scale[2], offset[2];
static unsigned calibrated;

/* The codec is initialized eagerly by the existing mode-setting patch.
 * Reject invalid conversions before calibrating/publishing a press. */
__attribute__((section(".entry")))
void touchReadXY(Touch *p)
{
    p->rawx = p->rawy = p->px = p->py = p->z1 = p->z2 = 0;
    if (!cdcTouchRead(p) || !p->rawx || !p->rawy) {
        p->rawx = p->rawy = 0;
        return;
    }
    if (!calibrated) {
        for (unsigned axis = 0; axis < 2; ++axis) {
            int a = R16(0x02fff628 + axis * 2);
            int b = R16(0x02fff62e + axis * 2);
            int pa = *(volatile uint8_t *)(0x02fff62c + axis);
            int pb = *(volatile uint8_t *)(0x02fff632 + axis);
            if (b-a >= 512 && pb > pa && b < 4096 &&
                pb <= (axis ? 191 : 255)) {
                scale[axis] = ((pb-pa) * 524288) / (b-a);
                offset[axis] = ((a+b)*scale[axis] - (pa+pb)*524288) / 2;
            } else {
                /* Missing/corrupt firmware calibration: usable full ADC range.
                 * Keep the same fallback used by the working v0.9 patch. */
                scale[axis] = (axis ? 192 : 256) * 128;
                offset[axis] = 0;
                calibrated |= 2u << axis;
            }
        }
        calibrated |= 1;
    }
    for (unsigned axis = 0; axis < 2; ++axis) {
        int raw = axis ? p->rawy : p->rawx;
        int px = (raw*scale[axis] - offset[axis] + scale[axis]/2) >> 19;
        int limit = axis ? 191 : 255;
        if (px < 0) px = 0;
        if (px > limit) px = limit;
        if (axis) p->py = px; else p->px = px;
    }
}

void twlModeReady(void)
{
    /* Preserve v0.9's mode-entry side effect even if the preceding async
     * disable-handler FIFO command was overtaken. No diagnostic hook remains. */
    R32(0x0380ffdc) = 0;
    calibrated = 0;
    cdcTouchInit();
}

__attribute__((section(".task")))
void taskARM7TouchScreen(void *unused)
{
    (void)unused;
    uint16_t keypad = R16(0x04000130);
    uint16_t keys = R16(0x04000136) | 0x40;
    R16(0x02fff242) = keypad;
    if (touchPenDown()) {
        Touch p;
        touchReadXY(&p);
        if (p.rawx && p.rawy) {
            volatile uint16_t *out = (volatile uint16_t *)0x02fff248;
            const uint16_t values[6] = {p.rawx,p.rawy,p.px,p.py,p.z1,p.z2};
            for (unsigned i = 0; i < 6; ++i) out[i] = values[i];
            keys &= ~0x40;
        }
    }
    R16(0x02fff240) = keys;
}
