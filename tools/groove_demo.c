/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – die zehn Grooves mit dem Preset MONSUN und seiner Sequenz, als WAV, dazu Pegel und Pumptiefe.
 *   cc -O2 -Idsp dsp/monsun_core.c tools/groove_demo.c -lm -o groove_demo && ./groove_demo out/ */
#include "monsun_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SR 44100
static int16_t DL[MN_DLY_FRAMES], DR[MN_DLY_FRAMES];
static float CHO[MN_CHO_FRAMES], REV[MN_REV_FLOATS], WIDE[MN_WIDE_FRAMES];
static mn_engine E;
static mn_patch P;
static mn_groove G;

static void w32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void w16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int st, frames = SR * 6;
    static const int BPM[MN_NSTYLES] = {124, 126, 132, 138, 128, 174, 140, 118, 128, 110};
    float *buf = malloc(sizeof(float) * 2 * (size_t)frames);
    double cpu = 0;
    for (st = 0; st < MN_NSTYLES; st++) {
        int i, j, bad = 0;
        double s = 0, peak = 0, minsc = 1;
        char path[256];
        FILE *f;
        clock_t t0;
        mn_preset(&P, st == 5 || st == 6 ? 1 : 0);
        P.bpm = (uint16_t)BPM[st];
        mn_groove_init(&G);
        G.on = 1;
        G.style = (uint8_t)st;
        G.var = st == 9 ? 90 : 30;
        mn_init(&E, SR, &P);
        mn_fx_memory(&E, DL, DR, CHO, REV, WIDE);
        mn_groove_use(&E, &G);
        mn_play(&E, 1);
        t0 = clock();
        for (i = 0; i < frames; i += 128) {
            float l[128], r[128];
            mn_render(&E, l, r, 128);
            if (E.sc_now < minsc)
                minsc = E.sc_now;
            for (j = 0; j < 128 && i + j < frames; j++) {
                buf[2 * (i + j)] = l[j];
                buf[2 * (i + j) + 1] = r[j];
            }
        }
        cpu += (double)(clock() - t0) / CLOCKS_PER_SEC;
        for (i = 0; i < 2 * frames; i++) {
            if (!isfinite(buf[i])) {
                bad++;
                buf[i] = 0;
            }
            s += buf[i] * buf[i];
            if (fabs(buf[i]) > peak)
                peak = fabs(buf[i]);
        }
        printf("%-10s peak %.2f rms %6.1f dB  pump down to %.2f  NaN %d\n", MN_STYLE_NAME[st], peak,
               10 * log10(s / (2 * frames) + 1e-12), minsc, bad);
        snprintf(path, sizeof path, "%s/groove_%02d_%s.wav", dir, st + 1, MN_STYLE_NAME[st]);
        f = fopen(path, "wb");
        if (f) {
            int n = frames * 2;
            fwrite("RIFF", 1, 4, f); w32(f, 36u + (uint32_t)n * 2u); fwrite("WAVEfmt ", 1, 8, f);
            w32(f, 16); w16(f, 1); w16(f, 2); w32(f, SR); w32(f, SR * 4); w16(f, 4); w16(f, 16);
            fwrite("data", 1, 4, f); w32(f, (uint32_t)n * 2u);
            for (i = 0; i < n; i++) {
                float c = buf[i] < -1.f ? -1.f : buf[i] > 1.f ? 1.f : buf[i];
                int16_t v = (int16_t)(c * 32767.f);
                fwrite(&v, 2, 1, f);
            }
            fclose(f);
        }
    }
    printf("render: %.2f%% of real time (this computer)\n", 100.0 * cpu / (MN_NSTYLES * 6.0));
    free(buf);
    return 0;
}
