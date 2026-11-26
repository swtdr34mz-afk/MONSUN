/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN by swtdr34mz: ein monophoner Synth für das M-VAVE FM-1 (auf fm1-base) nach dem Konzept des Dreadbox Typhon.
 * Dieser Header ist die App-Hälfte des Vertrags mit der Plattform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "MONSUN"
#define APP_SPLASH_RGB RGB(250, 212, 30)
#define APP_SPLASH                       /* app_splash: MONSUNs eigener Startbildschirm */
#define APP_BLOCK 128u                   /* Frames pro Audioblock: 2.9 ms bei 44.1 kHz (eine Tastatur: wenig Latenz) */
#define APP_TOUCH                        /* Anschlagdynamik von den Tasten (lib/touch.h) */
#define APP_CANVAS_PX (240u * 110u)      /* der Bildschirm in Streifen bis 110 Zeilen (das Delay braucht den Pool) */

/* der Speicher: die Einstellungen (mit dem Klang in Arbeit), die Panel-Kalibrierung (die der Plattform), und die
 * 16 Preset-Plätze in zwei Bänken zu je 8 */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BANK_A, OBJ_BANK_B, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_splash(void);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
