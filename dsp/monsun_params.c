/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – die Parameterliste (FM-1 und VST lesen dieselbe) und die Umrechnungen. Teil von monsun_core.c */

static const char *const SW_ONOFF[2] = {"OFF", "ON"};
static const char *const SW_SYNC3[2] = {"FREE", "BPM"};
static const char *const GMODE_NAME[2] = {"ON", "AUTO"};
static const char *const SYNC_NAME[4] = {"FREE", "KEY", "BPM", "BPM KEY"};
static const char *const LWAVE_NAME[5] = {"TRI", "SQUARE", "PULSE 25", "TRAPEZ", "CHAOS SAW"};
static const char *const DVS_NAME[MN_NDVS] = {"1", "2", "3", "4", "6", "8", "12", "16", "24", "32"};

const char *const MN_MODE_NAME[MN_NMODES] = {"LFO", "ENV", "RANDOM", "STEP"};
const char *const MN_X1_NAME[MN_NX1] = {"HARD CLIP", "FOLDBACK", "S-CURVE", "TRI CLIP", "SHRED", "BIT CRUSH"};
const char *const MN_X2_NAME[MN_NX2] = {"CHORUS", "ENSEMBLE", "FLANGER"};
const char *const MN_X3_NAME[MN_NX3] = {"DELAY", "PING PONG", "LARGE REV", "SOFT REV"};
const char *const MN_DEST_NAME[MN_NDEST] = {"CV", "CV1", "CV2", "WAV", "CUT", "VCA", "FX1", "FFM", "FX2", "FX3",
                                            "RES", "EGL", "HPF"};
const char *const MN_DEST_LONG[MN_NDEST] = {"PITCH", "VCO 1 PITCH", "VCO 2 PITCH", "WAVE", "CUTOFF", "VCA",
                                            "FX1 MIX", "FILTER FM", "FX2 MIX", "FX3 MIX", "RESONANCE",
                                            "EG LEVEL", "HIGH PASS"};
const char *const MN_DIV_NAME[MN_NDIV] = {"4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16",
                                          "1/16T", "1/32", "1/64"};
const float MN_DIV_BEATS[MN_NDIV] = {16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.6666667f, 0.5f, 0.3333333f, 0.25f, 0.1666667f,
                                     0.125f, 0.0625f};
const uint8_t MN_DVS[MN_NDVS] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};
const char *const MN_WAVE_NAME[MN_NWAVES] = {"TRI", "SAW", "2 SAW", "SAW+SQR", "2 SQR", "PULSE", "FM SOFT",
                                             "FM HARD"};

#define MOD_PARAMS(n)                                                                    \
    {"M" n " Mode", "MODE", 0, MK_LIST, MN_NMODES, MN_MODE_NAME},                       \
    {"M" n " Sync", "SYNC", 0, MK_LIST, 4, SYNC_NAME},                                  \
    {"M" n " LFO Rate", "RATE", 60, MK_UNI, 0, 0},                                      \
    {"M" n " LFO Wave", "WAVE", 0, MK_LIST, 5, LWAVE_NAME},                             \
    {"M" n " LFO Fade", "FADE", 64, MK_BIP, 0, 0},                                      \
    {"M" n " Env Attack", "A", 10, MK_UNI, 0, 0},                                       \
    {"M" n " Env Decay/Release", "D/R", 60, MK_UNI, 0, 0},                              \
    {"M" n " Env Sustain", "S", 0, MK_UNI, 0, 0},                                       \
    {"M" n " Random Rate", "RATE", 60, MK_UNI, 0, 0},                                   \
    {"M" n " Random Lag", "LAG", 20, MK_UNI, 0, 0},                                     \
    {"M" n " Random Probability", "PROB", 127, MK_UNI, 0, 0},                           \
    {"M" n " Steps", "#", 7, MK_LIST, MN_MSTEPS, 0}

const mn_param_info MN_PARAMS[MN_NP] = {
    {"Wave", "WAVE", 18, MK_UNI, 0, 0},
    {"Tune 2", "TUNE 2", 64, MK_BIP, 0, 0},
    {"Cutoff", "CUTOFF", 80, MK_UNI, 0, 0},
    {"Resonance", "RESON", 20, MK_UNI, 0, 0},
    {"Filter EG Level", "EG LEVEL", 90, MK_BIP, 0, 0},
    {"Filter Time", "FILT TIME", 64, MK_BIP, 0, 0},
    {"Amp Time", "AMP TIME", 64, MK_BIP, 0, 0},
    {"Filter Attack", "ATTACK", 0, MK_UNI, 0, 0},
    {"Filter Decay", "DECAY", 50, MK_UNI, 0, 0},
    {"Filter Sustain", "SUSTAIN", 40, MK_UNI, 0, 0},
    {"Filter Release", "RELEASE", 40, MK_UNI, 0, 0},
    {"Amp Attack", "ATTACK", 0, MK_UNI, 0, 0},
    {"Amp Decay", "DECAY", 50, MK_UNI, 0, 0},
    {"Amp Sustain", "SUSTAIN", 110, MK_UNI, 0, 0},
    {"Amp Release", "RELEASE", 40, MK_UNI, 0, 0},
    {"Glide", "GLIDE", 0, MK_UNI, 0, 0},
    {"VCO Level", "VCO LVL", 100, MK_UNI, 0, 0},
    {"VCF Key Track", "VCF TRACK", 64, MK_UNI, 0, 0},
    {"VCF FM", "VCF FFM", 0, MK_UNI, 0, 0},
    {"VCA Level", "VCA LVL", 110, MK_UNI, 0, 0},
    {"Legato", "LEGATO", 0, MK_SW, 2, SW_ONOFF},
    {"Glide Mode", "GLIDE MODE", 0, MK_LIST, 2, GMODE_NAME},
    {"Velocity > Cutoff", "VEL>CUT", 30, MK_UNI, 0, 0},
    {"Velocity > VCA", "VEL>VCA", 60, MK_UNI, 0, 0},
    {"High Pass Cutoff", "HP CUT", 0, MK_UNI, 0, 0},
    {"High Pass Resonance", "HP RES", 20, MK_UNI, 0, 0},
    {"FX1 Type", "TYPE", 2, MK_LIST, MN_NX1, MN_X1_NAME},
    {"FX1 Gain", "GAIN", 40, MK_UNI, 0, 0},
    {"FX1 Level", "LVL", 100, MK_UNI, 0, 0},
    {"FX1 Mix", "MIX", 0, MK_UNI, 0, 0},
    {"FX2 Type", "TYPE", 0, MK_LIST, MN_NX2, MN_X2_NAME},
    {"FX2 Depth", "DEPTH", 60, MK_UNI, 0, 0},
    {"FX2 Speed", "SPD", 30, MK_UNI, 0, 0},
    {"FX2 Width", "WIDTH", 127, MK_UNI, 0, 0},
    {"FX2 Feedback", "FEED", 64, MK_BIP, 0, 0},
    {"FX2 Mix", "MIX", 0, MK_UNI, 0, 0},
    {"FX3 Type", "TYPE", 1, MK_LIST, MN_NX3, MN_X3_NAME},
    {"FX3 Parameter 1", "TIME", 60, MK_UNI, 0, 0},
    {"FX3 Parameter 2", "TIME R", 64, MK_UNI, 0, 0},
    {"FX3 Feedback", "FEED", 50, MK_UNI, 0, 0},
    {"FX3 Damp", "DAMP", 64, MK_BIP, 0, 0},
    {"FX3 Mix", "MIX", 0, MK_UNI, 0, 0},
    {"FX3 Sync", "SYNC", 127, MK_SW, 2, SW_SYNC3},
    MOD_PARAMS("1"), MOD_PARAMS("2"), MOD_PARAMS("3"),
    {"Sequence Length", "LENG", 15, MK_LIST, MN_STEPS, 0},
    {"Sequence Divisions", "DVS", 7, MK_LIST, MN_NDVS, DVS_NAME},
    {"Sequence Probability", "PROB", 127, MK_UNI, 0, 0},
    {"Sequence Swing", "SWING", 0, MK_UNI, 0, 0},
    {"Storm", "STORM", 0, MK_UNI, 0, 0},
};

/* ------------------------------------------------------------- Umrechnungen --- */
float mn_cutoff_hz(int v) { return 20.0f * mn_exp2((float)v / 127.0f * 10.0f); }               /* 20 Hz .. 20 kHz */
float mn_env_ms(int v, int attack)
{
    float n = (float)v / 127.0f;
    return attack ? 1.0f * mn_exp2(n * 13.3f) : 2.0f * mn_exp2(n * 12.9f);   /* A 1 ms .. 10 s, D/R 2 ms .. 15 s */
}
float mn_lfo_hz(int v) { return 0.02f * mn_exp2((float)v / 127.0f * 11.3f); }                  /* 0.02 .. 50 Hz */
float mn_glide_ms(int v) { return v ? 2.0f * mn_exp2((float)v / 127.0f * 10.0f) : 0.0f; }      /* bis 2 s */
float mn_tune2_semis(int v)
{
    float b = ((float)v - 64.0f) / 63.0f;                /* fein um die Mitte (Schwebung), grob am Rand (Quinte, Oktave) */
    b = b < -1.0f ? -1.0f : b > 1.0f ? 1.0f : b;
    return 24.0f * b * b * b;
}
int mn_list_index(int id, int v)
{
    int n = MN_PARAMS[id].n;
    int i;
    if (n < 2)
        return 0;
    i = (v * (n - 1) + 63) / 127;
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
int mn_list_value(int id, int idx)
{
    int n = MN_PARAMS[id].n;
    if (n < 2)
        return 0;
    idx = idx < 0 ? 0 : idx >= n ? n - 1 : idx;
    return (idx * 127 + (n - 1) / 2) / (n - 1);
}
int mn_wave_index(int v)
{
    int i = (v * (MN_NWAVES - 1) + 63) / 127;
    return i < 0 ? 0 : i >= MN_NWAVES ? MN_NWAVES - 1 : i;
}

void mn_patch_init(mn_patch *pt)
{
    int i, m;
    char *z = (char *)pt;
    for (i = 0; i < (int)sizeof *pt; i++)
        z[i] = 0;
    for (i = 0; i < MN_NP; i++)           /* bei Listen steht im Standardwert der Eintrag */
        pt->p[i] = MN_PARAMS[i].kind == MK_LIST ? (uint8_t)mn_list_value(i, MN_PARAMS[i].def) : MN_PARAMS[i].def;
    for (m = 0; m < 3; m++) {
        for (i = 0; i < MN_NDEST; i++)
            pt->lvl[m][i] = 64;
        for (i = 0; i < MN_MSTEPS; i++)
            pt->mstep[m][i] = 64;
    }
    for (i = 0; i < MN_STEPS; i++) {
        pt->seq[i].note = 48;
        pt->seq[i].gate = 64;
        pt->seq[i].vel = 100;
    }
    pt->bpm = 120;
    {
        const char *n = "INIT";
        for (i = 0; n[i]; i++)
            pt->name[i] = n[i];
    }
}
