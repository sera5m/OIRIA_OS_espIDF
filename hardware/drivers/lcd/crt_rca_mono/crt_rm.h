/*
 * American CRT, mono composite, RCA jack.
 *
 * NTSC-M 240p (262 lines, ~15.73 kHz, ~60 Hz). Not 480i. US sets lock to this.
 * ESP32-S3 has no DAC, so the LCD i80 block DMA-clocks two data pins into a
 * resistor mixer. Three levels is the minimum a TV will sync:
 *
 *   sync tip  D0=0 D1=0
 *   black     D0=1 D1=0
 *   white     D0=1 D1=1
 *
 *   D0 (sync) -- 1.0 kΩ --+
 *   D1 (luma) -- 470 Ω  --+---- RCA center
 *   RCA sleeve ------------ GND
 *   The TV's 75 Ω termination is the load. Do not add another 75 Ω.
 *
 * D2-D7 and WR are claimed by the DMA block and must be left unconnected.
 * Select it with screen_set_driver(&external_screen_driver) after crt_rm_init().
 */
#ifndef MAIN_HARDWARE_DRIVERS_LCD_CRT_RCA_MONO_CRT_RM_H_
#define MAIN_HARDWARE_DRIVERS_LCD_CRT_RCA_MONO_CRT_RM_H_

#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRT_SYNC_GPIO
#define CRT_SYNC_GPIO GPIO_NUM_3
#endif
#ifndef CRT_LUMA_GPIO
#define CRT_LUMA_GPIO GPIO_NUM_38
#endif
/* WR strobe. Not part of the video signal. Leave the pin open. */
#ifndef CRT_WR_GPIO
#define CRT_WR_GPIO GPIO_NUM_28
#endif

/* 8 MHz sample clock, 508 data bytes + 1 command byte ≈ 15.72 kHz line rate. */
#define CRT_PCLK_HZ        8000000
#define CRT_LINE_SAMPLES   508
#define CRT_LINES_PER_FIELD 262
#define CRT_VSYNC_LINES    4
#define CRT_PRE_BLANK      10
#define CRT_ACTIVE_LINES   240
#define CRT_HSYNC_SAMPLES  37
#define CRT_BACK_PORCH     40
#define CRT_OUT_PIXELS     200
#define CRT_SAMPLES_PER_PX 2

void crt_rm_init(void);
void crt_rm_stop(void);
/* RGB565 front buffer -> 1bpp, picked up on the next field. */
void crt_rm_present(void);
bool crt_rm_running(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_HARDWARE_DRIVERS_LCD_CRT_RCA_MONO_CRT_RM_H_ */
