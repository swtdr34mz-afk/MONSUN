/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – die 16 Werks-Presets: Klang, Modulatoren und Sequenz. Teil von monsun_core.c
 *
 * Spickzettel (alle Werte 0..127):
 *   WAVE      0 TRI, 18 SAW, 36 2 SAW, 54 SAW+SQR, 73 2 SQR, 91 PULSE, 109 FM SOFT, 127 FM HARD
 *   CUTOFF    20 Hz * 2^(v/12.7)       50 = 300 Hz, 62 = 600 Hz, 74 = 1 kHz, 87 = 2 kHz, 99 = 4 kHz
 *   TUNE 2    64 = 0, 74 = +0.1, 101 = +5, 106 = +7, 114 = +12, 14 = -12 Halbtöne
 *   ATTACK    32 = 10 ms, 63 = 100 ms, 85 = 500 ms, 104 = 2 s
 *   DECAY/REL 56 = 100 ms, 71 = 300 ms, 88 = 1 s, 104 = 3 s
 *   LFO RATE  26 = 0.1 Hz, 44 = 0.3 Hz, 64 = 1 Hz, 89 = 5 Hz
 *   LVL       64 = 0, darüber +, darunter -; Tonhöhe: 64 + 63 * sqrt(Halbtöne / 24) */

typedef struct { uint8_t id, v; } pv_t;
typedef struct { uint8_t m, dest, v; } lv_t;
typedef struct {
    const char *name;
    uint16_t bpm;
    const pv_t *pv;
    uint8_t npv;
    const lv_t *lv;
    uint8_t nlv;
    const uint8_t *mstep;                /* 16 Werte für M1..M3 im STEP-Modus (0: keine) */
    uint8_t mstep_m;
    const uint8_t *notes;                /* 16 Steps: 0 = Pause, sonst die Note */
    const uint8_t *gates;                /* 0 = Pause, 127 = TIE; ohne: 64 */
    const uint8_t *vels;                 /* 0 = 100 */
} preset_t;

#define M(m, o) (uint8_t)(P_MOD(m) + (o))
#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

/* 1 MONSUN: zwei verstimmte Sägen, satter Filter, ein Ping-Pong im Achtel */
static const pv_t PV_MONSUN[] = {
    {P_WAVE, 36}, {P_TUNE2, 74}, {P_CUTOFF, 56}, {P_RESON, 62}, {P_EGLVL, 104}, {P_FD, 62}, {P_FS, 30}, {P_FR, 50},
    {P_AD, 70}, {P_AS, 100}, {P_AR, 45}, {P_X1TYPE, 51}, {P_X1GAIN, 50}, {P_X1MIX, 45}, {P_X3TYPE, 42},
    {P_X3P1, 60}, {P_X3FEED, 55}, {P_X3MIX, 28}, {P_X3DAMP, 30}, {P_SDVS, 99}, {M(0, MO_LRATE), 40}};
static const lv_t LV_MONSUN[] = {{0, D_CUT, 74}};
static const uint8_t NT_MONSUN[16] = {33, 33, 45, 33, 0, 33, 43, 33, 36, 0, 33, 45, 33, 40, 43, 45};
static const uint8_t GT_MONSUN[16] = {80, 40, 50, 40, 0, 40, 60, 40, 90, 0, 40, 50, 40, 60, 50, 40};
static const uint8_t VL_MONSUN[16] = {127, 80, 100, 80, 0, 90, 110, 80, 127, 0, 85, 100, 80, 100, 90, 110};

/* 2 DARK SUB: Säge und Sub-Oktave, kein Hall, gleitet nur gebunden */
static const pv_t PV_SUB[] = {
    {P_WAVE, 36}, {P_TUNE2, 14}, {P_CUTOFF, 46}, {P_RESON, 25}, {P_EGLVL, 84}, {P_FD, 58}, {P_FS, 25},
    {P_AD, 60}, {P_AS, 115}, {P_AR, 35}, {P_GLIDE, 45}, {P_GMODE, 127}, {P_X1TYPE, 51}, {P_X1GAIN, 30},
    {P_X1MIX, 35}, {P_SDVS, 99}};
static const uint8_t NT_SUB[16] = {28, 0, 28, 31, 0, 28, 0, 35, 28, 0, 28, 31, 33, 0, 31, 26};
static const uint8_t GT_SUB[16] = {110, 0, 60, 127, 0, 70, 0, 60, 110, 0, 60, 60, 127, 0, 60, 80};

/* 3 ACID RAIN: eine Säge, Resonanz am Anschlag, harter Clip, gebundene Steps gleiten */
static const pv_t PV_ACID[] = {
    {P_WAVE, 18}, {P_CUTOFF, 44}, {P_RESON, 108}, {P_EGLVL, 112}, {P_FD, 50}, {P_FS, 0}, {P_FR, 40},
    {P_AD, 62}, {P_AS, 90}, {P_AR, 30}, {P_GLIDE, 38}, {P_GMODE, 127}, {P_LEGATO, 127}, {P_VELCUT, 80},
    {P_X1TYPE, 0}, {P_X1GAIN, 62}, {P_X1LVL, 80}, {P_X1MIX, 55}, {P_X3TYPE, 42}, {P_X3P1, 75},
    {P_X3FEED, 45}, {P_X3MIX, 30}, {P_X3DAMP, 20}, {P_SDVS, 99}, {P_SSWING, 30}};
static const uint8_t NT_ACID[16] = {36, 36, 48, 36, 39, 0, 36, 51, 36, 46, 48, 0, 36, 43, 36, 41};
static const uint8_t GT_ACID[16] = {50, 40, 127, 50, 40, 0, 50, 127, 40, 50, 60, 0, 40, 127, 50, 60};
static const uint8_t VL_ACID[16] = {127, 70, 110, 80, 127, 0, 70, 127, 80, 100, 127, 0, 70, 120, 80, 100};

/* 4 FM BELLS: weiche FM eine Oktave über, kurze Hüllkurve, großer Hall */
static const pv_t PV_BELLS[] = {
    {P_WAVE, 112}, {P_TUNE2, 114}, {P_CUTOFF, 104}, {P_RESON, 10}, {P_EGLVL, 80}, {P_FD, 70}, {P_FS, 0},
    {P_AD, 92}, {P_AS, 0}, {P_AR, 88}, {P_X2TYPE, 0}, {P_X2MIX, 35}, {P_X3TYPE, 85}, {P_X3P1, 30},
    {P_X3P2, 100}, {P_X3FEED, 90}, {P_X3MIX, 55}, {M(0, MO_MODE), 42}, {M(0, MO_EA), 0}, {M(0, MO_EDR), 70}};
static const lv_t LV_BELLS[] = {{0, D_WAVE, 50}};

/* 5 TYPHOON LEAD: Säge + Rechteck auf der Quinte, Vibrato blendet ein, STORM halb */
static const pv_t PV_LEAD[] = {
    {P_WAVE, 54}, {P_TUNE2, 106}, {P_CUTOFF, 80}, {P_RESON, 48}, {P_EGLVL, 90}, {P_FD, 66}, {P_FS, 70},
    {P_AA, 20}, {P_AD, 70}, {P_AS, 110}, {P_AR, 58}, {P_GLIDE, 52}, {P_GMODE, 127}, {P_X3TYPE, 0},
    {P_X3P1, 82}, {P_X3P2, 92}, {P_X3FEED, 50}, {P_X3MIX, 32}, {P_X3SYNC, 127}, {P_STORM, 60},
    {M(0, MO_LRATE), 90}, {M(0, MO_LFADE), 100}, {M(0, MO_SYNC), 42}};
static const lv_t LV_LEAD[] = {{0, D_CV, 72}};

/* 6 WET SEASON: langsames Pad, Ensemble, großer Hall, STORM fast ganz */
static const pv_t PV_PAD[] = {
    {P_WAVE, 36}, {P_TUNE2, 76}, {P_CUTOFF, 66}, {P_RESON, 30}, {P_EGLVL, 80}, {P_FA, 80}, {P_FD, 90},
    {P_FS, 60}, {P_FR, 95}, {P_AA, 85}, {P_AD, 80}, {P_AS, 120}, {P_AR, 98}, {P_X2TYPE, 64}, {P_X2MIX, 75},
    {P_X2SPD, 40}, {P_X3TYPE, 85}, {P_X3P2, 120}, {P_X3FEED, 100}, {P_X3MIX, 60}, {P_STORM, 95},
    {M(0, MO_LRATE), 30}};
static const lv_t LV_PAD[] = {{0, D_CUT, 80}, {0, D_WAVE, 70}};

/* 7 THUNDER: harte FM, Foldback, Zufall im Filter */
static const pv_t PV_THUNDER[] = {
    {P_WAVE, 127}, {P_TUNE2, 34}, {P_CUTOFF, 92}, {P_RESON, 40}, {P_EGLVL, 70}, {P_FD, 80}, {P_FS, 20},
    {P_AD, 90}, {P_AS, 60}, {P_AR, 90}, {P_X1TYPE, 25}, {P_X1GAIN, 80}, {P_X1MIX, 80}, {P_X3TYPE, 85},
    {P_X3P2, 127}, {P_X3FEED, 95}, {P_X3MIX, 45}, {M(1, MO_MODE), 85}, {M(1, MO_RRATE), 70}, {M(1, MO_RLAG), 40}};
static const lv_t LV_THUNDER[] = {{1, D_CUT, 100}, {1, D_FX1, 84}};

/* 8 HYDRO PLUCK: schmaler Puls, kurz, Chorus und Ping Pong im Achtel */
static const pv_t PV_PLUCK[] = {
    {P_WAVE, 91}, {P_CUTOFF, 52}, {P_RESON, 45}, {P_EGLVL, 110}, {P_FD, 52}, {P_FS, 0}, {P_FR, 50},
    {P_AD, 60}, {P_AS, 0}, {P_AR, 55}, {P_X2TYPE, 0}, {P_X2MIX, 45}, {P_X3TYPE, 42}, {P_X3P1, 60},
    {P_X3FEED, 60}, {P_X3MIX, 40}, {P_SDVS, 99}};
static const uint8_t NT_PLUCK[16] = {57, 64, 69, 72, 57, 64, 71, 76, 55, 62, 67, 71, 55, 62, 69, 74};
static const uint8_t VL_PLUCK[16] = {120, 80, 90, 100, 110, 80, 90, 100, 120, 80, 90, 100, 110, 80, 90, 127};

/* 9 SQUELCH: Säge + Rechteck, viel Resonanz, M1 als Hüllkurve auf den Filter */
static const pv_t PV_SQUELCH[] = {
    {P_WAVE, 54}, {P_CUTOFF, 40}, {P_RESON, 116}, {P_EGLVL, 64}, {P_AD, 62}, {P_AS, 100}, {P_AR, 40},
    {P_X1TYPE, 102}, {P_X1GAIN, 30}, {P_X1MIX, 30}, {P_SDVS, 99},
    {M(0, MO_MODE), 42}, {M(0, MO_EA), 20}, {M(0, MO_EDR), 58}, {M(0, MO_ES), 10}};
static const lv_t LV_SQUELCH[] = {{0, D_CUT, 120}, {0, D_RES, 54}};
static const uint8_t NT_SQUELCH[16] = {31, 0, 43, 31, 0, 31, 41, 0, 31, 43, 0, 31, 46, 0, 43, 38};

/* 10 RAIN DROPS: Dreieck, Zufall im Sechzehntel auf Tonhöhe und Filter, kurz, viel Hall */
static const pv_t PV_RAIN[] = {
    {P_WAVE, 6}, {P_CUTOFF, 84}, {P_RESON, 70}, {P_EGLVL, 90}, {P_FD, 50}, {P_FS, 0}, {P_AD, 58}, {P_AS, 0},
    {P_AR, 60}, {P_X3TYPE, 85}, {P_X3P1, 50}, {P_X3P2, 110}, {P_X3FEED, 95}, {P_X3MIX, 60},
    {M(0, MO_MODE), 85}, {M(0, MO_SYNC), 85}, {M(0, MO_RRATE), 95}, {M(0, MO_RLAG), 0}, {M(0, MO_RPROB), 90}};
static const lv_t LV_RAIN[] = {{0, D_CV, 104}, {0, D_CUT, 90}};
static const uint8_t NT_RAIN[16] = {72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72};
static const uint8_t GT_RAIN[16] = {30, 0, 30, 30, 0, 30, 0, 30, 30, 30, 0, 30, 0, 30, 30, 0};

/* 11 CYCLONE: STORM ganz, Flanger, ein Trapez-LFO dreht die Welle */
static const pv_t PV_CYCLONE[] = {
    {P_WAVE, 36}, {P_TUNE2, 70}, {P_CUTOFF, 76}, {P_RESON, 55}, {P_EGLVL, 74}, {P_AA, 40}, {P_AS, 120},
    {P_AR, 80}, {P_X2TYPE, 127}, {P_X2MIX, 70}, {P_X2SPD, 20}, {P_X2FEED, 110}, {P_X3TYPE, 127},
    {P_X3FEED, 80}, {P_X3MIX, 40}, {P_STORM, 127}, {M(1, MO_LWAVE), 95}, {M(1, MO_LRATE), 38}};
static const lv_t LV_CYCLONE[] = {{1, D_WAVE, 92}, {1, D_CUT, 74}};

/* 12 LOW PRESSURE: Drone; drei langsame Modulatoren auf Filter, Welle und Filter-FM */
static const pv_t PV_DRONE[] = {
    {P_WAVE, 73}, {P_TUNE2, 101}, {P_CUTOFF, 52}, {P_RESON, 70}, {P_EGLVL, 64}, {P_FFM, 20}, {P_AA, 90},
    {P_AS, 127}, {P_AR, 104}, {P_X2TYPE, 64}, {P_X2MIX, 50}, {P_X3TYPE, 85}, {P_X3P1, 70}, {P_X3P2, 127},
    {P_X3FEED, 110}, {P_X3MIX, 70}, {P_STORM, 70},
    {M(0, MO_LRATE), 26}, {M(1, MO_LRATE), 18}, {M(2, MO_MODE), 85},
    {M(2, MO_RRATE), 30}, {M(2, MO_RLAG), 110}};
static const lv_t LV_DRONE[] = {{0, D_CUT, 90}, {1, D_WAVE, 84}, {2, D_FFM, 96}};

/* 13 MONO BRASS: Säge, Filter-Attack, Vibrato blendet ein */
static const pv_t PV_BRASS[] = {
    {P_WAVE, 22}, {P_TUNE2, 73}, {P_CUTOFF, 54}, {P_RESON, 22}, {P_EGLVL, 100}, {P_FA, 55}, {P_FD, 72},
    {P_FS, 50}, {P_AA, 32}, {P_AS, 110}, {P_AR, 52}, {P_X2TYPE, 64}, {P_X2MIX, 30}, {P_X3TYPE, 127},
    {P_X3MIX, 35}, {M(0, MO_LRATE), 88}, {M(0, MO_LFADE), 110}, {M(0, MO_SYNC), 42}};
static const lv_t LV_BRASS[] = {{0, D_CV, 70}};

/* 14 STEP STORM: der Step-Modulator klettert durch den Filter */
static const pv_t PV_STEP[] = {
    {P_WAVE, 40}, {P_CUTOFF, 40}, {P_RESON, 90}, {P_EGLVL, 74}, {P_AD, 70}, {P_AS, 90}, {P_AR, 40},
    {P_X1TYPE, 51}, {P_X1MIX, 40}, {P_X3TYPE, 42}, {P_X3P1, 75}, {P_X3MIX, 30}, {P_STORM, 40}, {P_SDVS, 99},
    {M(0, MO_MODE), 127}, {M(0, MO_SCOUNT), 59}};
static const lv_t LV_STEP[] = {{0, D_CUT, 108}};
static const uint8_t MS_STEP[16] = {0, 30, 70, 20, 100, 50, 10, 127, 0, 40, 90, 30, 110, 60, 20, 80};
static const uint8_t NT_STEP[16] = {38, 38, 38, 50, 38, 38, 48, 38, 38, 38, 50, 38, 45, 38, 48, 50};

/* 15 CRUSHED: der Bitcrusher ganz, ein harter Rhythmus */
static const pv_t PV_CRUSH[] = {
    {P_WAVE, 54}, {P_CUTOFF, 80}, {P_RESON, 40}, {P_EGLVL, 90}, {P_FD, 56}, {P_FS, 10}, {P_AD, 62},
    {P_AS, 60}, {P_X1TYPE, 127}, {P_X1GAIN, 50}, {P_X1LVL, 40}, {P_X1MIX, 100}, {P_X3TYPE, 42},
    {P_X3P1, 50}, {P_X3MIX, 25}, {P_SDVS, 99}, {M(0, MO_MODE), 85}, {M(0, MO_SYNC), 85}, {M(0, MO_RRATE), 80}};
static const lv_t LV_CRUSH[] = {{0, D_FX1, 40}, {0, D_CUT, 84}};
static const uint8_t NT_CRUSH[16] = {36, 36, 0, 48, 36, 0, 46, 36, 0, 36, 48, 0, 36, 51, 0, 43};

/* 16 EYE OF STORM: weiche FM, der Step-Modulator auf der Welle, Ensemble, ruhiger Hall */
static const pv_t PV_EYE[] = {
    {P_WAVE, 100}, {P_TUNE2, 106}, {P_CUTOFF, 82}, {P_RESON, 30}, {P_EGLVL, 74}, {P_AA, 70}, {P_AD, 85},
    {P_AS, 110}, {P_AR, 96}, {P_X2TYPE, 64}, {P_X2MIX, 60}, {P_X3TYPE, 127}, {P_X3P2, 110}, {P_X3FEED, 100},
    {P_X3MIX, 55}, {P_STORM, 60}, {P_SDVS, 71}, {M(2, MO_MODE), 127}, {M(2, MO_SCOUNT), 25}};
static const lv_t LV_EYE[] = {{2, D_WAVE, 40}};
static const uint8_t MS_EYE[16] = {64, 100, 30, 127, 64, 100, 30, 127, 64, 64, 64, 64, 64, 64, 64, 64};

static const preset_t PRESETS[MN_NPRESETS] = {
    {"MONSUN", 118, PV_MONSUN, N(PV_MONSUN), LV_MONSUN, N(LV_MONSUN), 0, 0, NT_MONSUN, GT_MONSUN, VL_MONSUN},
    {"DARKSUB", 124, PV_SUB, N(PV_SUB), 0, 0, 0, 0, NT_SUB, GT_SUB, 0},
    {"ACIDRN", 128, PV_ACID, N(PV_ACID), 0, 0, 0, 0, NT_ACID, GT_ACID, VL_ACID},
    {"FMBELLS", 100, PV_BELLS, N(PV_BELLS), LV_BELLS, N(LV_BELLS), 0, 0, 0, 0, 0},
    {"TYPHOON", 120, PV_LEAD, N(PV_LEAD), LV_LEAD, N(LV_LEAD), 0, 0, 0, 0, 0},
    {"WETSEAS", 90, PV_PAD, N(PV_PAD), LV_PAD, N(LV_PAD), 0, 0, 0, 0, 0},
    {"THUNDER", 100, PV_THUNDER, N(PV_THUNDER), LV_THUNDER, N(LV_THUNDER), 0, 0, 0, 0, 0},
    {"HYDRO", 112, PV_PLUCK, N(PV_PLUCK), 0, 0, 0, 0, NT_PLUCK, 0, VL_PLUCK},
    {"SQUELCH", 126, PV_SQUELCH, N(PV_SQUELCH), LV_SQUELCH, N(LV_SQUELCH), 0, 0, NT_SQUELCH, 0, 0},
    {"RAINDRP", 96, PV_RAIN, N(PV_RAIN), LV_RAIN, N(LV_RAIN), 0, 0, NT_RAIN, GT_RAIN, 0},
    {"CYCLONE", 110, PV_CYCLONE, N(PV_CYCLONE), LV_CYCLONE, N(LV_CYCLONE), 0, 0, 0, 0, 0},
    {"LOWPRES", 80, PV_DRONE, N(PV_DRONE), LV_DRONE, N(LV_DRONE), 0, 0, 0, 0, 0},
    {"BRASS", 110, PV_BRASS, N(PV_BRASS), LV_BRASS, N(LV_BRASS), 0, 0, 0, 0, 0},
    {"STEPSTM", 122, PV_STEP, N(PV_STEP), LV_STEP, N(LV_STEP), MS_STEP, 0, NT_STEP, 0, 0},
    {"CRUSHED", 130, PV_CRUSH, N(PV_CRUSH), LV_CRUSH, N(LV_CRUSH), 0, 0, NT_CRUSH, 0, 0},
    {"EYESTRM", 84, PV_EYE, N(PV_EYE), LV_EYE, N(LV_EYE), MS_EYE, 2, 0, 0, 0},
};

const char *mn_preset_name(int i) { return i >= 0 && i < MN_NPRESETS ? PRESETS[i].name : ""; }

void mn_preset(mn_patch *pt, int idx)
{
    const preset_t *pr;
    int i;
    if (idx < 0 || idx >= MN_NPRESETS)
        return;
    pr = &PRESETS[idx];
    mn_patch_init(pt);
    for (i = 0; i < MN_NAMELEN; i++)
        pt->name[i] = 0;
    for (i = 0; i < MN_NAMELEN - 1 && pr->name[i]; i++)
        pt->name[i] = pr->name[i];
    pt->bpm = pr->bpm;
    for (i = 0; i < pr->npv; i++)
        pt->p[pr->pv[i].id] = pr->pv[i].v;
    for (i = 0; i < pr->nlv; i++)
        pt->lvl[pr->lv[i].m][pr->lv[i].dest] = pr->lv[i].v;
    if (pr->mstep)
        for (i = 0; i < MN_MSTEPS; i++)
            pt->mstep[pr->mstep_m][i] = pr->mstep[i];
    for (i = 0; i < MN_STEPS; i++) {     /* 16 Steps, zweimal (die Länge zeigt die ersten 16) */
        int s = i & 15;
        mn_step *st = &pt->seq[i];
        if (pr->notes) {
            st->note = pr->notes[s] ? pr->notes[s] : 48;
            st->gate = !pr->notes[s] ? MN_REST : pr->gates && pr->gates[s] ? pr->gates[s] : 64;
            if (pr->gates && pr->notes[s] && pr->gates[s] == 0)
                st->gate = MN_REST;
            st->vel = pr->vels && pr->vels[s] ? pr->vels[s] : 100;
        } else {                         /* ohne Sequenz: eine schlichte Achtel-Linie auf dem Grundton */
            static const uint8_t PLAIN[16] = {48, 0, 48, 0, 55, 0, 48, 0, 60, 0, 48, 0, 55, 0, 58, 0};
            st->note = PLAIN[s] ? PLAIN[s] : 48;
            st->gate = PLAIN[s] ? 64 : MN_REST;
            st->vel = 100;
        }
    }
}
