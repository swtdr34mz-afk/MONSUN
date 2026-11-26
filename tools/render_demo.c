/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – Offline-Renderer am PC: jedes Preset als WAV (mit seiner Sequenz, oder gespielte Noten), dazu Pegel und
 * Rechenzeit. cc -O2 -Idsp dsp/monsun_core.c tools/render_demo.c -lm -o render_demo && ./render_demo out/ */
#include "monsun_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR 44100
#define BLOCK 128

static void w32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void w16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void write_wav(const char *path, const float *d, int frames)
{
    FILE *f = fopen(path, "wb");
    int i, n = frames * 2;
    if (!f) {
        perror(path);
        return;
    }
    fwrite("RIFF", 1, 4, f); w32(f, 36u + (uint32_t)n * 2u); fwrite("WAVEfmt ", 1, 8, f);
    w32(f, 16); w16(f, 1); w16(f, 2); w32(f, SR); w32(f, SR * 4); w16(f, 4); w16(f, 16);
    fwrite("data", 1, 4, f); w32(f, (uint32_t)n * 2u);
    for (i = 0; i < n; i++) {
        float c = d[i] < -1.f ? -1.f : d[i] > 1.f ? 1.f : d[i];
        int16_t s = (int16_t)(c * 32767.f);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

static int16_t DL[MN_DLY_FRAMES], DR[MN_DLY_FRAMES];
static float CHO[MN_CHO_FRAMES], REV[MN_REV_FLOATS], WIDE[MN_WIDE_FRAMES];
static mn_engine E;
static mn_patch P;

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int pr, frames = SR * 6;
    float *buf = malloc(sizeof(float) * 2 * (size_t)frames);
    double total_cpu = 0;
    for (pr = 0; pr < MN_NPRESETS; pr++) {
        int i, bad = 0;
        double peak = 0, sum = 0;
        clock_t t0;
        char path[512];
        mn_preset(&P, pr);
        mn_init(&E, SR, &P);
        mn_fx_memory(&E, DL, DR, CHO, REV, WIDE);
        /* Presets mit Sequenz: der Sequencer läuft; die anderen: drei gehaltene Noten nacheinander */
        if (pr == 3 || pr == 4 || pr == 5 || pr == 6 || pr == 10 || pr == 11 || pr == 12 || pr == 15) {
            static const int NOTES[3] = {48, 55, 60};
            t0 = clock();
            for (i = 0; i < frames; i += BLOCK) {
                float l[BLOCK], r[BLOCK];
                int j, k = i / (SR * 3 / 2);
                if (i % (SR * 3 / 2) == 0 && k < 3)
                    mn_note_on(&E, NOTES[k] - (pr == 11 || pr == 6 ? 12 : 0), 100);
                if (i % (SR * 3 / 2) == SR && k < 3 && pr != 11)
                    mn_note_off(&E, NOTES[k] - (pr == 6 ? 12 : 0));
                mn_render(&E, l, r, BLOCK);
                for (j = 0; j < BLOCK && i + j < frames; j++) {
                    buf[2 * (i + j)] = l[j];
                    buf[2 * (i + j) + 1] = r[j];
                }
            }
        } else {
            mn_play(&E, 1);
            t0 = clock();
            for (i = 0; i < frames; i += BLOCK) {
                float l[BLOCK], r[BLOCK];
                int j;
                mn_render(&E, l, r, BLOCK);
                for (j = 0; j < BLOCK && i + j < frames; j++) {
                    buf[2 * (i + j)] = l[j];
                    buf[2 * (i + j) + 1] = r[j];
                }
            }
        }
        total_cpu += (double)(clock() - t0) / CLOCKS_PER_SEC;
        for (i = 0; i < 2 * frames; i++) {
            if (!isfinite(buf[i])) {
                bad++;
                buf[i] = 0;
            }
            if (fabs(buf[i]) > peak)
                peak = fabs(buf[i]);
            sum += buf[i] * buf[i];
        }
        printf("%2d %-8s peak %5.2f  rms %6.1f dB  NaN %d\n", pr + 1, mn_preset_name(pr), peak,
               10.0 * log10(sum / (2 * frames) + 1e-12), bad);
        snprintf(path, sizeof path, "%s/%02d_%s.wav", dir, pr + 1, mn_preset_name(pr));
        write_wav(path, buf, frames);
    }
    printf("render: %.1f%% of real time (this computer)\n", 100.0 * total_cpu / (MN_NPRESETS * 6.0));
    free(buf);
    return 0;
}
