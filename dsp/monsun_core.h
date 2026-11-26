/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – der Klangkern. Reines C, keine Abhängigkeit von der FM-1-Plattform: dieselben Dateien laufen in der
 * Firmware (app.c) und später in einem VST-Plugin.
 *
 * Ein monophoner Synth nach dem Konzept des Dreadbox Typhon: zwei VCOs, deren Wellen-Kombinationen ein Regler
 * (WAVE) überblendet, ein 24-dB-Ladder-Tiefpass, zwei ADSR-Hüllkurven mit TIME-Reglern, drei Modulatoren
 * (LFO / Hüllkurve / Zufall / Step) mit mehreren Zielen, drei FX-Blöcke (Verzerrung, Chorus/Ensemble/Flanger,
 * Delay/Reverb) und ein 32-Step-Notensequencer. Dazu STORM: verstimmte Oszillator-Kopien und Stereo-Breite.
 *
 * Ein Klang (mn_patch) besteht aus Bytes 0..127, genau so, wie die Firmware ihn speichert. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- die Parameter --- */
/* ein Modulator: 12 Parameter hintereinander (MO_* ist der Abstand zum Anfang) */
enum { MO_MODE, MO_SYNC, MO_LRATE, MO_LWAVE, MO_LFADE, MO_EA, MO_EDR, MO_ES, MO_RRATE, MO_RLAG, MO_RPROB, MO_SCOUNT,
       MO_N };

enum {
    /* die linke Panelseite */
    P_WAVE, P_TUNE2, P_CUTOFF, P_RESON, P_EGLVL, P_FTIME, P_ATIME,
    /* Hüllkurven */
    P_FA, P_FD, P_FS, P_FR, P_AA, P_AD, P_AS, P_AR,
    /* SET */
    P_GLIDE, P_VCOLVL, P_TRACK, P_FFM, P_VCALVL, P_LEGATO, P_GMODE, P_VELCUT, P_VELVCA,
    /* der Hochpass vor den FX (FX1-Seite am Original) */
    P_HPCUT, P_HPRES,
    /* FX1: Verzerrung */
    P_X1TYPE, P_X1GAIN, P_X1LVL, P_X1MIX,
    /* FX2: Chorus, Ensemble, Flanger */
    P_X2TYPE, P_X2DEPTH, P_X2SPD, P_X2WIDTH, P_X2FEED, P_X2MIX,
    /* FX3: Delay, Ping Pong, Reverbs */
    P_X3TYPE, P_X3P1, P_X3P2, P_X3FEED, P_X3DAMP, P_X3MIX, P_X3SYNC,
    /* Modulatoren */
    P_M1, P_M2 = P_M1 + MO_N, P_M3 = P_M2 + MO_N,
    /* Notensequencer */
    P_SLEN = P_M3 + MO_N, P_SDVS, P_SPROB, P_SSWING,
    /* STORM (ALGORITHM) */
    P_STORM,
    MN_NP
};
#define P_MOD(m) (P_M1 + (m) * MO_N)

/* Art eines Parameters: Anzeige und Bedienung */
enum { MK_UNI, MK_BIP, MK_SW, MK_LIST };
typedef struct {
    const char *name;     /* lang (VST) */
    const char *label;    /* kurz, fürs Display */
    uint8_t def;          /* 0..127 */
    uint8_t kind;         /* MK_* */
    uint8_t n;            /* MK_LIST: Einträge */
    const char *const *names;   /* MK_LIST / MK_SW */
} mn_param_info;
extern const mn_param_info MN_PARAMS[MN_NP];

/* Listen */
enum { MODE_LFO, MODE_ENV, MODE_RND, MODE_STEP, MN_NMODES };
enum { SYNC_FREE, SYNC_KEY, SYNC_BPM, SYNC_BPMKEY };
enum { LW_TRI, LW_SQR, LW_PULSE, LW_TRAP, LW_CHAOS };
enum { X1_HARD, X1_FOLD, X1_SCURVE, X1_TRI, X1_SHRED, X1_CRUSH, MN_NX1 };
enum { X2_CHORUS, X2_ENSEMBLE, X2_FLANGER, MN_NX2 };
enum { X3_DELAY, X3_PING, X3_LARGE, X3_SOFT, MN_NX3 };
extern const char *const MN_MODE_NAME[MN_NMODES];
extern const char *const MN_X1_NAME[MN_NX1], *const MN_X2_NAME[MN_NX2], *const MN_X3_NAME[MN_NX3];

/* die Ziele der Modulatoren (wie am Typhon; die drei letzten sind dort frei wählbar) */
enum { D_CV, D_CV1, D_CV2, D_WAVE, D_CUT, D_VCA, D_FX1, D_FFM, D_FX2, D_FX3, D_RES, D_EGL, D_HPF, MN_NDEST };
extern const char *const MN_DEST_NAME[MN_NDEST];
extern const char *const MN_DEST_LONG[MN_NDEST];

/* Tempo-Teilungen: für die Modulatoren (BPM) und das Delay (SYNC) */
#define MN_NDIV 12
extern const char *const MN_DIV_NAME[MN_NDIV];
extern const float MN_DIV_BEATS[MN_NDIV];   /* Länge in Vierteln */
/* der Sequencer: Steps pro Takt */
#define MN_NDVS 10
extern const uint8_t MN_DVS[MN_NDVS];

/* die WAVE-Stellungen (der Regler blendet zwischen ihnen) */
#define MN_NWAVES 8
extern const char *const MN_WAVE_NAME[MN_NWAVES];

/* ---------------------------------------------------------------- ein Klang --- */
#define MN_STEPS 32
#define MN_MSTEPS 16
#define MN_REST 0        /* gate 0: Pause */
#define MN_TIE 127       /* gate 127: an den nächsten Step gebunden */
typedef struct {
    uint8_t note, gate, vel;   /* note: MIDI 24..96 */
} mn_step;

#define MN_NAMELEN 8
typedef struct {
    char name[MN_NAMELEN];               /* 7 Zeichen wie am Typhon, mit 0 */
    uint16_t bpm;                        /* 30..300 */
    uint8_t p[MN_NP];
    uint8_t lvl[3][MN_NDEST];            /* der LVL jedes Modulators zu jedem Ziel: 64 = 0 */
    uint8_t mstep[3][MN_MSTEPS];         /* die Werte der Step-Modulatoren 0..127 */
    mn_step seq[MN_STEPS];
} mn_patch;

void mn_patch_init(mn_patch *pt);        /* der INITIAL PRESET */

/* --------------------------------------------------------------- die Bausteine --- */
typedef struct { float phase; } mn_osc;
typedef struct { float s0, s1, s2, s3, t0, t1, t2, last, p3; } mn_ladder;
typedef struct { float level; int stage; float a_inc, d_coef, r_coef, sus; } mn_adsr;   /* stage 0 aus, 1 A, 2 D/S, 3 R */

typedef struct {
    float ph, out, target, held, fade, fade_t, chaos;
    int stage, cycles;                   /* ENV: 0 aus, 1 A, 2 D/S, 3 R */
    int step, step_n;                    /* STEP: der Step, und wie oft schon gezählt */
    float step_to;
    uint32_t rs;
} mn_mod;

/* FX-Speicher: den gibt der Host (FM-1: der RAM-Pool) */
#define MN_DLY_FRAMES 44100              /* 1 s, wie am Typhon */
#define MN_CHO_FRAMES 2048               /* FX2 */
#define MN_REV_FLOATS 19200              /* die Reverbs: 12 Kämme, 8 Allpässe */
#define MN_WIDE_FRAMES 1024              /* STORM */

typedef struct {
    /* FX1 */
    float hp_l, hp_b;                    /* der Hochpass (SVF) */
    float cr_v;
    int cr_n;
    /* FX2 */
    float *cho;
    int cho_pos;
    float cho_ph, cho_fb;
    /* FX3 */
    int16_t *dl, *dr;
    int dpos;
    float dsl, dsr, dfl, dfr, dhl, dhr;
    float *comb[12], *ap[8], cf[12];     /* Kämme 0..5 links, 6..11 rechts; Allpässe 0..3 links, 4..7 rechts */
    int clen[12], cpos[12], alen[8], apos[8];
    float rdh, rdh2;
    /* STORM */
    float *wide;
    int wpos;
    float wph;
    int ready;
} mn_fx;

/* ---- GROOVE: Kick, Drums und eine pumpende Sidechain (MONSUNs eigene Zugabe) ---- */
#define MN_NSTYLES 10
extern const char *const MN_STYLE_NAME[MN_NSTYLES];
typedef struct {
    uint8_t on;                          /* 1: läuft mit PLAY */
    uint8_t style;                       /* 0..MN_NSTYLES-1 */
    uint8_t kick, drums, pump;           /* 0..127: KICK (kurz .. boomig), DRUMS (Lautstärke), PUMP (Tiefe) */
    uint8_t rel, hats, tune, var;        /* 0..127: RELEASE der Pumpe, HATS, TUNE (64 = 0), VAR (Variation) */
} mn_groove;
void mn_groove_init(mn_groove *g);

typedef struct { int16_t off; uint8_t step; } mn_gtrig;
typedef struct {
    float kph, kenv, kt, kfreq, kdec, kclick, kdrive, kpitch;   /* Kick */
    float cenv, ct, cbp1, cbp2;          /* Clap / Snare (Rauschen im Bandpass) */
    float sph, senv;                     /* Snare: der Ton */
    float henv, oenv, hlp1, hlp2;        /* Hats: geschlossen, offen */
    float pph, pfreq, penv;              /* Perc */
    float sc_t;                          /* Samples seit dem letzten Kick (Sidechain) */
    float lvl_k, lvl_c, lvl_h, lvl_o, lvl_p;
    uint32_t rs;
} mn_drums;

/* Ereignisse von der Bedienung an den Audio-Teil (Ring, ein Schreiber / ein Leser) */
enum { MEV_ON, MEV_OFF, MEV_ALLOFF, MEV_PLAY, MEV_STOP, MEV_CONT, MEV_CLOCK, MEV_BEND, MEV_HOLD, MEV_ONF };
typedef struct { uint8_t type, a, b; } mn_ev;
#define MN_NEV 64

typedef struct {
    float sr;
    mn_patch *pt;                        /* der Klang, den die Bedienung bearbeitet */
    int dirty;                           /* der Klang hat sich geändert: neu umrechnen */
    /* Ereignisse */
    mn_ev ev[MN_NEV];
    volatile uint32_t ev_w, ev_r;
    /* Noten: der Stapel gehaltener Tasten (die letzte zählt) */
    uint8_t held[12];
    int nheld;
    int hold;                            /* HOLD: losgelassene Tasten klingen weiter */
    uint8_t latched[12];
    int nlatched;
    int note;                            /* die klingende Note, -1 keine */
    float vel, pitch, pitch_to, glide_coef, bend, bend_range;
    int gate;
    /* Stimme */
    mn_osc o1, o2, o3, o4;               /* o3 / o4: die STORM-Kopien */
    mn_ladder vcf;
    mn_adsr fe, ae;
    float hb[46];
    int hb_pos;
    float dc_x, dc_y, drift1, drift2, storm_wob;
    uint32_t rs;
    int eco;
    /* Modulatoren */
    mn_mod mod[3];
    float md[MN_NDEST];                  /* die Summe zu jedem Ziel, jetzt */
    /* umgerechnet */
    float u_wave, u_tune2, u_cut, u_res, u_egl, u_track, u_ffm, u_vco, u_vca, u_velcut, u_velvca;
    float u_glide, u_storm, u_hp, u_hpq;
    int legato, auto_glide;
    float lvl[3][MN_NDEST];
    /* Sequencer und Takt */
    int running, cur, tie_hold;
    float to_next, gate_left;
    int sw_odd;
    int transpose;
    int seq_note;                        /* die Note, die der Sequencer hält (-1 keine) */
    uint32_t counter, last_clock;
    float ext_bpm;
    int ext, ext_ticks, ext_running;
    int beat_tick;                       /* für die Step-Modulatoren */
    float step_clock;                    /* Samples bis zum nächsten Modulator-Step */
    /* GROOVE */
    const mn_groove *gv;
    int g_step;
    float g_to_next;
    mn_gtrig gq[16];
    int gqn;
    mn_drums dr;
    float sc_now;                        /* die Sidechain jetzt: 1 offen, 0 ganz geduckt (Anzeige) */
    uint32_t kick_count;                 /* zählt die Kicks (Anzeige: ein Blitz je Kick) */
    /* FX */
    mn_fx fx;
    float mono[256];
    float level_now;                     /* für die Anzeige */
    float cut_hz;                        /* der Cutoff, wie er gerade steht (Anzeige) */
} mn_engine;

/* ------------------------------------------------------------- die Schnittstelle --- */
void mn_init(mn_engine *e, float sr, mn_patch *pt);
void mn_fx_memory(mn_engine *e, int16_t *dl, int16_t *dr, float *cho, float *rev, float *wide);
void mn_changed(mn_engine *e);           /* nach jeder Änderung am Klang */
void mn_groove_use(mn_engine *e, const mn_groove *g);   /* die GROOVE-Einstellungen (gehören der Bedienung) */
int mn_groove_step(const mn_engine *e);  /* 0..31, -1: steht */
int mn_groove_hit(const mn_engine *e, int lane, int step);   /* fürs Display: spielt die Spur dort? (0..3) */
void mn_render(mn_engine *e, float *l, float *r, int n);   /* n <= 256 */

/* aus der Bedienung (Hauptschleife): landen im Ring und wirken im nächsten Block */
void mn_note_on(mn_engine *e, int note, int vel);
void mn_note_off(mn_engine *e, int note);
void mn_note_on_play(mn_engine *e, int note, int vel);   /* spielt auch bei laufendem Sequencer (RECORD) */
void mn_all_off(mn_engine *e);
void mn_play(mn_engine *e, int on);
void mn_continue(mn_engine *e);
void mn_midi_clock(mn_engine *e);
void mn_bend(mn_engine *e, int v14);     /* 0..16383, 8192 Mitte */
void mn_hold(mn_engine *e, int on);

float mn_bpm(const mn_engine *e);
int mn_step_now(const mn_engine *e);     /* -1: gestoppt */
int mn_seq_len(const mn_engine *e);
int mn_dvs(const mn_engine *e);          /* Steps pro Takt */
float mn_mod_out(const mn_engine *e, int m);   /* für die Anzeige: -1..1 */
int mn_mod_step(const mn_engine *e, int m);

/* Umrechnungen für die Anzeige */
float mn_cutoff_hz(int v);
float mn_env_ms(int v, int attack);
float mn_lfo_hz(int v);
float mn_tune2_semis(int v);
float mn_glide_ms(int v);
int mn_list_index(int id, int v);        /* MK_LIST: welcher Eintrag */
int mn_list_value(int id, int idx);      /* der Bytewert eines Eintrags */
int mn_wave_index(int v);

/* Werks-Presets */
#define MN_NPRESETS 16
void mn_preset(mn_patch *pt, int i);
const char *mn_preset_name(int i);

#ifdef __cplusplus
}
#endif
