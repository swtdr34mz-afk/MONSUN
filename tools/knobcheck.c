/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – jeder Parameter von ganz links nach ganz rechts: ändert sich der Klang? (Pegel und Helligkeit)
 *   cc -O2 -Idsp dsp/monsun_core.c tools/knobcheck.c -lm -o knobcheck && ./knobcheck */
#include "monsun_core.h"
#include <math.h>
#include <stdio.h>

#define SR 44100
static int16_t DL[MN_DLY_FRAMES], DR[MN_DLY_FRAMES];
static float CHO[MN_CHO_FRAMES], REV[MN_REV_FLOATS], WIDE[MN_WIDE_FRAMES];
static mn_engine E;
static mn_patch P;

/* eine Note, 0.8 s: Pegel (dB) und Helligkeit (Anteil der Differenzen: grob die Höhen) */
static void measure(const mn_patch *src, int seq, double *db, double *hi, double *wid)
{
    int i, j;
    double s = 0, d = 0, side = 0;
    float pl = 0;
    P = *src;
    mn_init(&E, SR, &P);
    mn_fx_memory(&E, DL, DR, CHO, REV, WIDE);
    if (seq)
        mn_play(&E, 1);
    else
        mn_note_on(&E, 48, 100);
    for (i = 0; i < SR * 8 / 10; i += 128) {
        float l[128], r[128];
        if (i / 128 == SR / 2 / 128 && !seq)
            mn_note_off(&E, 48);
        mn_render(&E, l, r, 128);
        for (j = 0; j < 128; j++) {
            float m = 0.5f * (l[j] + r[j]);
            s += m * m;
            d += (m - pl) * (m - pl);
            side += 0.25 * (l[j] - r[j]) * (l[j] - r[j]);
            pl = m;
        }
    }
    *db = 10 * log10(s / (SR * 0.8) + 1e-12);
    *hi = s > 0 ? d / s : 0;
    *wid = 10 * log10(side / (SR * 0.8) + 1e-12);
}

int main(void)
{
    int id, dead = 0;
    mn_patch base;
    mn_patch_init(&base);
    base.p[P_X1MIX] = 60;
    base.p[P_X2MIX] = 60;
    base.p[P_X3MIX] = 60;
    for (id = 0; id < MN_NP; id++) {
        mn_patch a = base, b = base;
        double da, ha, wa, db, hb, wb;
        a.p[id] = 0;
        b.p[id] = 127;
        if (id >= P_M1 && id < P_M1 + 3 * MO_N) {   /* ein Modulator: auf den Cutoff, im passenden Modus */
            int m = (id - P_M1) / MO_N, o = (id - P_M1) % MO_N, mode = o >= MO_RRATE ? (o == MO_SCOUNT ? 127 : 85) : o >= MO_EA ? 42 : 0;
            a.lvl[m][D_CUT] = b.lvl[m][D_CUT] = 127;
            if (o != MO_MODE)
                a.p[P_MOD(m) + MO_MODE] = b.p[P_MOD(m) + MO_MODE] = (uint8_t)mode;
            a.mstep[m][1] = b.mstep[m][1] = 127;
        }
        measure(&a, id >= P_SLEN, &da, &ha, &wa);
        measure(&b, id >= P_SLEN, &db, &hb, &wb);
        if (fabs(da - db) < 0.3 && fabs(ha - hb) < 0.01 * (ha + hb + 1e-6) && fabs(wa - wb) < 0.5) {
            printf("NO EFFECT %-24s  %6.1f dB %6.3f | %6.1f dB %6.3f\n", MN_PARAMS[id].name, da, ha, db, hb);
            dead++;
        } else {
            printf("ok        %-24s  %6.1f dB %6.3f w%6.1f | %6.1f dB %6.3f w%6.1f\n", MN_PARAMS[id].name, da, ha, wa, db, hb, wb);
        }
    }
    printf("%d without effect\n", dead);
    return 0;
}
