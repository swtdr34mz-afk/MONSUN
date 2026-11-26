/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN: Bedienung, Seiten, Presets, Sequencer-Editor, Bildschirm. Teil des Unity-Builds (device/unity.c,
 * host/plat_host.c), nach der Plattform: gfx.c und announce.c sind sichtbar. Der Klang ist dsp/monsun_core.c.
 *
 * Bedienung (wie das Panel des Typhon, auf die Knöpfe des FM-1 verteilt)
 *   K1..K4     die vier Regler der Seite
 *   HOME       die linke Panelseite: WAVE, TUNE 2, FILTER TIME, AMP TIME
 *   SEL        FILTER: CUTOFF, RESON, EG LEVEL, HP CUT
 *   ENV        FILTER EG (A D S R), nochmal: AMP EG
 *   LFO        M1, M1 ZIELE, M2, M2 ZIELE, M3, M3 ZIELE (nacheinander)
 *   EDIT       SET: GLIDE, VCO LVL, VCF TRACK, VCF FFM; nochmal: VCA LVL, LEGATO, GLIDE MODE, HP RES
 *   FX         FX1, FX2, FX3, FX+ (nacheinander)
 *   GLO        TEMPO, VEL>CUT, VEL>VCA, DELAY SYNC; nochmal: die globalen Einstellungen
 *   SEQ        der Notensequencer: STEP, NOTE, GATE, VEL; nochmal: LENG, DVS, PROB, SWING
 *   SELECT     CUTOFF (immer)         ALGORITHM   STORM          PRESET   die 16 Plätze
 *   PLAY       Sequencer Start / Stop REC   auf den SEQ-Seiten: RECORD; sonst 1 s halten: das Werks-Preset
 *   SAVE       speichern: PRESET wählt den Platz, SAVE nochmal speichert
 *   ARP        HOLD (Noten klingen weiter); auf den SEQ-Seiten: Step kopieren / einfügen
 *   OCT- OCT+  die Oktave der Tasten
 *   Tasten     spielen (mit Anschlag); läuft der Sequencer, transponieren sie ihn (C3 = unverändert) */
#include "fastmath.h"
#include "monsun_core.h"

#define SR 44100.0f

static mn_engine eng;
static mn_patch edit;                    /* der Klang in Arbeit (den die Engine spielt) */

/* ------------------------------------------------------------ Speicher --- */
#define SET_MAGIC 0x314E534Du            /* "MSN1" */
#define BANK_MAGIC 0x424E534Du           /* "MSNB" */
#define NSLOTS 16
typedef struct {
    uint32_t magic;
    uint8_t slot, edited, oct, bend, mch, touch, rsv[2];
    mn_patch edit;
} settings_t;
typedef struct {
    uint32_t magic;
    mn_patch p[8];
} bank_t;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "die Einstellungen passen nicht in ein Speicherobjekt");
_Static_assert(sizeof(bank_t) <= PLAT_STORE_MAX, "eine Bank passt nicht in ein Speicherobjekt");
static settings_t set;
static bank_t bank[2];
static uint8_t set_dirty;
static uint32_t dirty_at;

static int patch_ok(const mn_patch *p)
{
    int i, m;
    for (i = 0; i < MN_NP; i++)
        if (p->p[i] > 127u)
            return 0;
    for (m = 0; m < 3; m++)
        for (i = 0; i < MN_NDEST; i++)
            if (p->lvl[m][i] > 127u)
                return 0;
    for (i = 0; i < MN_STEPS; i++)
        if (p->seq[i].note < 12u || p->seq[i].note > 108u || p->seq[i].gate > 127u || p->seq[i].vel > 127u)
            return 0;
    return p->bpm >= 30u && p->bpm <= 300u && p->name[MN_NAMELEN - 1] == 0;
}

static mn_patch *slot_patch(int s) { return &bank[s >= 8].p[s & 7]; }

static void bank_load(int b)
{
    int i, ok = plat_store_load(OBJ_BANK_A + (uint32_t)b, &bank[b], sizeof bank[b]) == (int)sizeof bank[b]
                && bank[b].magic == BANK_MAGIC;
    for (i = 0; ok && i < 8; i++)
        ok = patch_ok(&bank[b].p[i]);
    if (!ok) {                           /* noch nichts gespeichert: die Werks-Presets */
        bank[b].magic = BANK_MAGIC;
        for (i = 0; i < 8; i++)
            mn_preset(&bank[b].p[i], b * 8 + i);
    }
}

static void dirty(void)
{
    set_dirty = 1;
    dirty_at = plat_ms();
}

static void edited(void)                 /* der Klang wurde geändert (noch nicht gespeichert) */
{
    set.edited = 1;
    mn_changed(&eng);
    dirty();
}

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.slot >= NSLOTS || !patch_ok(&set.edit) || set.oct > 6u || set.bend < 1u || set.bend > 12u
        || set.mch > 16u) {
        memset(&set, 0, sizeof set);
        set.magic = SET_MAGIC;
        set.oct = 3;                     /* 3 = keine Verschiebung: die Tasten F2 .. G4 */
        set.bend = 2;
        set.touch = 1;
        set.edit = *slot_patch(0);
    }
    edit = set.edit;
}

static void settings_save(void)
{
    set.magic = SET_MAGIC;
    set.edit = edit;
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
        set_dirty = 0;
}

/* ---------------------------------------------- an die Engine (aus der Hauptschleife) --- */
/* die Ereignisse gehen in einen Ring, den der Audio-Interrupt leert; MIDI schreibt im Interrupt hinein, darum
 * hier mit gesperrten Interrupts */
static void e_on(int note, int vel, int force)
{
    uint32_t f = plat_lock();
    if (force)
        mn_note_on_play(&eng, note, vel);
    else
        mn_note_on(&eng, note, vel);
    plat_unlock(f);
}
static void e_off(int note)
{
    uint32_t f = plat_lock();
    mn_note_off(&eng, note);
    plat_unlock(f);
}
static void e_play(int on)
{
    uint32_t f = plat_lock();
    mn_play(&eng, on);
    plat_unlock(f);
}
static void e_alloff(void)
{
    uint32_t f = plat_lock();
    mn_all_off(&eng);
    plat_unlock(f);
}
static void e_hold(int on)
{
    uint32_t f = plat_lock();
    mn_hold(&eng, on);
    plat_unlock(f);
}

/* ------------------------------------------------------------------ Seiten --- */
enum { PG_MAIN, PG_FILTER, PG_FENV, PG_AENV, PG_M1, PG_M1D, PG_M2, PG_M2D, PG_M3, PG_M3D, PG_SET1, PG_SET2,
       PG_FX1, PG_FX2, PG_FX3, PG_FXP, PG_GLO1, PG_GLO2, PG_SEQ1, PG_SEQ2, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"PANEL", "FILTER", "FILTER EG", "AMP EG", "M1", "M1 DEST", "M2",
                                              "M2 DEST", "M3", "M3 DEST", "SET", "SET 2", "FX1 DIST", "FX2 ENHANCE",
                                              "FX3 REFLECT", "FX MORE", "GLOBAL", "SETTINGS", "SEQUENCER",
                                              "SEQ SETUP"};

enum { K_NONE, K_PARAM, K_LVL, K_DEST, K_MSEL, K_MVAL, K_SSEL, K_SNOTE, K_SGATE, K_SVEL, K_TEMPO, K_BEND, K_MCH,
       K_TOUCH };
typedef struct { uint8_t kind, id; } kdef;

static const kdef PAGE_K[NPAGES][4] = {
    {{K_PARAM, P_WAVE}, {K_PARAM, P_TUNE2}, {K_PARAM, P_FTIME}, {K_PARAM, P_ATIME}},
    {{K_PARAM, P_CUTOFF}, {K_PARAM, P_RESON}, {K_PARAM, P_EGLVL}, {K_PARAM, P_HPCUT}},
    {{K_PARAM, P_FA}, {K_PARAM, P_FD}, {K_PARAM, P_FS}, {K_PARAM, P_FR}},
    {{K_PARAM, P_AA}, {K_PARAM, P_AD}, {K_PARAM, P_AS}, {K_PARAM, P_AR}},
    {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},          /* die Modulatoren: nach ihrem Modus (kdef_of) */
    {{K_PARAM, P_GLIDE}, {K_PARAM, P_VCOLVL}, {K_PARAM, P_TRACK}, {K_PARAM, P_FFM}},
    {{K_PARAM, P_VCALVL}, {K_PARAM, P_LEGATO}, {K_PARAM, P_GMODE}, {K_PARAM, P_HPRES}},
    {{K_PARAM, P_X1TYPE}, {K_PARAM, P_X1GAIN}, {K_PARAM, P_X1LVL}, {K_PARAM, P_X1MIX}},
    {{K_PARAM, P_X2TYPE}, {K_PARAM, P_X2DEPTH}, {K_PARAM, P_X2SPD}, {K_PARAM, P_X2MIX}},
    {{K_PARAM, P_X3TYPE}, {K_PARAM, P_X3P1}, {K_PARAM, P_X3P2}, {K_PARAM, P_X3MIX}},
    {{K_PARAM, P_X2WIDTH}, {K_PARAM, P_X2FEED}, {K_PARAM, P_X3FEED}, {K_PARAM, P_X3DAMP}},
    {{K_TEMPO, 0}, {K_PARAM, P_VELCUT}, {K_PARAM, P_VELVCA}, {K_PARAM, P_X3SYNC}},
    {{K_BEND, 0}, {K_MCH, 0}, {K_TOUCH, 0}, {K_NONE, 0}},
    {{K_SSEL, 0}, {K_SNOTE, 0}, {K_SGATE, 0}, {K_SVEL, 0}},
    {{K_PARAM, P_SLEN}, {K_PARAM, P_SDVS}, {K_PARAM, P_SPROB}, {K_PARAM, P_SSWING}},
};

static struct {
    char msg[2][24];
    uint32_t msg_until;
    char big[2][20];
    uint32_t big_until;
    uint32_t knob_at[4], enc_at[NE];
    uint32_t keys, btn;
    uint8_t page, playing, hold, redraw;
    uint8_t dest[3], msel[3];            /* je Modulator: das Ziel und der Step, die gerade bearbeitet werden */
    uint8_t ssel;                        /* der Step im Sequencer-Editor */
    uint8_t rec;                         /* RECORD auf den SEQ-Seiten */
    int8_t copied;                       /* ein kopierter Step (-1: keiner) */
    mn_step clip;
    uint8_t saving, save_slot;           /* SAVE: der Platz wird gewählt */
    uint32_t save_until;
    uint32_t rec_t;                      /* REC gehalten seit */
    int8_t key_note[NKEYS];              /* die Note jeder gedrückten Taste */
    int32_t step_drawn, lvl_drawn;
    uint32_t load_at, load_shown;
    uint8_t hot;
    float scope[3][96];                  /* die Ausgänge der Modulatoren, zuletzt */
    uint8_t scope_pos;
} ui;

static float fabsf_(float x) { return x < 0.0f ? -x : x; }

static uint32_t txt_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void dec1_text(float v, char *b)  /* eine Nachkommastelle */
{
    int32_t d = (int32_t)(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f));
    if (d < 0) {
        *b++ = '-';
        d = -d;
    }
    int_text(d / 10, b);
    text_cat(b, ".");
    int_text(d % 10, b + txt_len(b));
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1200u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void say_big(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 19u && a[i]; i++)
        ui.big[0][i] = a[i];
    ui.big[0][i] = 0;
    for (i = 0; i < 19u && b[i]; i++)
        ui.big[1][i] = b[i];
    ui.big[1][i] = 0;
    ui.big_until = plat_ms() + 900u;
    ui.redraw = 1;
    if (ann_on()) {
        char t[44] = "";
        text_cat(t, ui.big[1]);
        text_cat(t, " ");
        text_cat(t, ui.big[0]);
        ann_say(t, 0, ANN_NOW);
    }
}

/* die Note als Name, wie am Typhon: C3 = MIDI 60 */
static void note_text(int n, char *t)
{
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    t[0] = 0;
    text_cat(t, NN[n % 12]);
    int_text(n / 12 - 2, t + txt_len(t));
}

static void ms_text(float ms, char *t)
{
    t[0] = 0;
    if (ms < 1000.0f) {
        int_text((int32_t)(ms + 0.5f), t);
        text_cat(t, "MS");
    } else {
        dec1_text(ms / 1000.0f, t);
        text_cat(t, "S");
    }
}
static void hz_text(float hz, char *t)
{
    t[0] = 0;
    if (hz < 10.0f) {
        dec1_text(hz, t);
        text_cat(t, "HZ");
    } else if (hz < 1000.0f) {
        int_text((int32_t)(hz + 0.5f), t);
        text_cat(t, "HZ");
    } else {
        dec1_text(hz / 1000.0f, t);
        text_cat(t, "K");
    }
}

/* ---------------------------------------------------------------- die Regler --- */
static int mod_of_page(int pg) { return pg >= PG_M1 && pg <= PG_M3D ? (pg - PG_M1) / 2 : -1; }
static int mode_of(int m) { return mn_list_index(P_MOD(m) + MO_MODE, edit.p[P_MOD(m) + MO_MODE]); }
static int scount(int m) { return mn_list_index(P_MOD(m) + MO_SCOUNT, edit.p[P_MOD(m) + MO_SCOUNT]) + 1; }

static kdef kdef_of(int k)
{
    kdef d = {K_NONE, 0};
    int m = mod_of_page(ui.page);
    if (m < 0)
        return PAGE_K[ui.page][k];
    if ((ui.page - PG_M1) & 1) {         /* die Ziele: DEST, LVL, SYNC */
        static const uint8_t KD[4] = {K_DEST, K_LVL, K_PARAM, K_NONE};
        d.kind = KD[k];
        d.id = d.kind == K_PARAM ? (uint8_t)(P_MOD(m) + MO_SYNC) : (uint8_t)m;
        return d;
    }
    if (k == 0) {
        d.kind = K_PARAM;
        d.id = (uint8_t)(P_MOD(m) + MO_MODE);
        return d;
    }
    switch (mode_of(m)) {
    case MODE_LFO: d.kind = K_PARAM; d.id = (uint8_t)(P_MOD(m) + MO_LRATE + k - 1); break;
    case MODE_ENV: d.kind = K_PARAM; d.id = (uint8_t)(P_MOD(m) + MO_EA + k - 1); break;
    case MODE_RND: d.kind = K_PARAM; d.id = (uint8_t)(P_MOD(m) + MO_RRATE + k - 1); break;
    default:                             /* STEP: #, STEP, ->LVL */
        d.kind = k == 1 ? K_PARAM : k == 2 ? K_MSEL : K_MVAL;
        d.id = k == 1 ? (uint8_t)(P_MOD(m) + MO_SCOUNT) : (uint8_t)m;
        break;
    }
    return d;
}

/* der Name eines Reglers (die FX3-Regler heißen je nach Typ anders) */
static const char *knob_label(kdef d)
{
    switch (d.kind) {
    case K_PARAM:
        if (d.id == P_X3P1 || d.id == P_X3P2) {
            int t = mn_list_index(P_X3TYPE, edit.p[P_X3TYPE]);
            if (t == X3_DELAY)
                return d.id == P_X3P1 ? "TIME L" : "TIME R";
            if (t == X3_PING)
                return d.id == P_X3P1 ? "TIME" : "-";
            return d.id == P_X3P1 ? "PRE" : "SIZE";
        }
        if (d.id == P_X1GAIN && mn_list_index(P_X1TYPE, edit.p[P_X1TYPE]) == X1_CRUSH)
            return "RATE";
        if (d.id == P_X1LVL && mn_list_index(P_X1TYPE, edit.p[P_X1TYPE]) == X1_CRUSH)
            return "BITS";
        if (d.id == P_X3SYNC)
            return "DLY SYNC";
        if (ui.page == PG_FXP) {             /* FX MORE: welcher Block */
            if (d.id == P_X2WIDTH)
                return "FX2 WIDTH";
            if (d.id == P_X2FEED)
                return "FX2 FEED";
            if (d.id == P_X3FEED)
                return "FX3 FEED";
            if (d.id == P_X3DAMP)
                return "FX3 DAMP";
        }
        return MN_PARAMS[d.id].label;
    case K_LVL: return "LVL";
    case K_DEST: return "DEST";
    case K_MSEL: return "STEP";
    case K_MVAL: return "->LVL";
    case K_SSEL: return "STEP";
    case K_SNOTE: return "NOTE";
    case K_SGATE: return "GATE";
    case K_SVEL: return "VEL";
    case K_TEMPO: return "TEMPO";
    case K_BEND: return "BEND";
    case K_MCH: return "MIDI CH";
    case K_TOUCH: return "VELOCITY";
    default: return "";
    }
}

/* der Wert eines Reglers als Zahl (0..127 für die Anzeige des Bogens), -1: keiner */
static int knob_get(kdef d)
{
    switch (d.kind) {
    case K_PARAM: return edit.p[d.id];
    case K_LVL: return edit.lvl[d.id][ui.dest[d.id]];
    case K_DEST: return ui.dest[d.id] * 127 / (MN_NDEST - 1);
    case K_MSEL: return scount(d.id) > 1 ? ui.msel[d.id] * 127 / (scount(d.id) - 1) : 0;
    case K_MVAL: return edit.mstep[d.id][ui.msel[d.id]];
    case K_SSEL: return ui.ssel * 127 / (MN_STEPS - 1);
    case K_SNOTE: return (edit.seq[ui.ssel].note - 24) * 127 / 72;
    case K_SGATE: return edit.seq[ui.ssel].gate;
    case K_SVEL: return edit.seq[ui.ssel].vel;
    case K_TEMPO: return ((int)edit.bpm - 30) * 127 / 270;
    case K_BEND: return (set.bend - 1) * 127 / 11;
    case K_MCH: return set.mch * 127 / 16;
    case K_TOUCH: return set.touch ? 127 : 0;
    default: return -1;
    }
}

static int knob_bip(kdef d)
{
    if (d.kind == K_LVL)
        return 1;
    return d.kind == K_PARAM && MN_PARAMS[d.id].kind == MK_BIP;
}

static void val_text(kdef d, char *t)
{
    int v = knob_get(d);
    t[0] = 0;
    switch (d.kind) {
    case K_PARAM: {
        const mn_param_info *pi = &MN_PARAMS[d.id];
        int id = d.id, m = id >= P_M1 && id < P_M1 + 3 * MO_N ? (id - P_M1) / MO_N : -1, o = m >= 0 ? (id - P_M1) % MO_N : -1;
        if (pi->kind == MK_LIST) {
            int i = mn_list_index(id, v);
            if (pi->names)
                text_cat(t, pi->names[i]);
            else
                int_text(i + 1, t);
            return;
        }
        if (pi->kind == MK_SW) {
            text_cat(t, pi->names[v >= 64]);
            return;
        }
        if (o == MO_LRATE || o == MO_RRATE) {
            int sync = mn_list_index(P_MOD(m) + MO_SYNC, edit.p[P_MOD(m) + MO_SYNC]);
            if (sync >= SYNC_BPM)
                text_cat(t, MN_DIV_NAME[(v * (MN_NDIV - 1) + 63) / 127]);
            else
                hz_text(mn_lfo_hz(v), t);
            return;
        }
        if (o == MO_EA || o == MO_EDR) {
            ms_text(mn_env_ms(v, o == MO_EA), t);
            return;
        }
        if (o == MO_RLAG) {
            ms_text((float)v * (float)v * (2000.0f / 16129.0f), t);
            return;
        }
        switch (id) {
        case P_WAVE: text_cat(t, MN_WAVE_NAME[mn_wave_index(v)]); return;
        case P_TUNE2: {
            float s = mn_tune2_semis(v);
            if (s > 0.04f)
                text_cat(t, "+");
            dec1_text(s, t + txt_len(t));
            return;
        }
        case P_CUTOFF: hz_text(mn_cutoff_hz(v), t); return;
        case P_HPCUT:
            if (!v)
                text_cat(t, "OFF");
            else
                hz_text(20.0f * fm_exp2f((float)v / 127.0f * 10.0f), t);
            return;
        case P_FTIME:
        case P_ATIME: {                  /* bis 10x langsamer, bis 6x schneller */
            float b = ((float)v - 64.0f) / 63.0f;
            if (b > 1.0f)
                b = 1.0f;
            if (v == 64) {
                text_cat(t, "X1");
                return;
            }
            text_cat(t, b < 0.0f ? "SLOW X" : "FAST X");
            dec1_text(b < 0.0f ? fm_exp2f(-b * 3.3219f) : fm_exp2f(b * 2.585f), t + txt_len(t));
            return;
        }
        case P_FA:
        case P_AA: ms_text(mn_env_ms(v, 1), t); return;
        case P_FD:
        case P_FR:
        case P_AD:
        case P_AR: ms_text(mn_env_ms(v, 0), t); return;
        case P_GLIDE:
            if (!v)
                text_cat(t, "OFF");
            else
                ms_text(mn_glide_ms(v), t);
            return;
        case P_SSWING: int_text(50 + v * 25 / 127, t); text_cat(t, "%"); return;
        case P_TRACK: int_text(v * 200 / 127, t); text_cat(t, "%"); return;
        case P_X3P1:
        case P_X3P2: {
            int ty = mn_list_index(P_X3TYPE, edit.p[P_X3TYPE]);
            if (ty == X3_PING && id == P_X3P2) {
                text_cat(t, "-");
                return;
            }
            if (ty <= X3_PING) {
                if (edit.p[P_X3SYNC] >= 64)
                    text_cat(t, MN_DIV_NAME[3 + (v * (MN_NDIV - 4) + 63) / 127]);
                else
                    ms_text(10.0f * fm_exp2f((float)v / 127.0f * 6.64f), t);
                return;
            }
            if (id == P_X3P1) {
                ms_text((float)v * (100.0f / 127.0f), t);
                return;
            }
            break;
        }
        default: break;
        }
        if (pi->kind == MK_BIP) {
            int b = (v - 64) * 100 / 63;
            if (b > 0)
                text_cat(t, "+");
            int_text(b, t + txt_len(t));
            return;
        }
        int_text(v * 100 / 127, t);
        return;
    }
    case K_LVL: {
        int dd = ui.dest[d.id];
        float b = ((float)v - 64.0f) / 63.0f;
        if (dd <= D_CV2) {               /* Tonhöhe in Halbtönen */
            float s = b * (b < 0.0f ? -b : b) * 24.0f;
            if (s > 0.04f)
                text_cat(t, "+");
            dec1_text(s, t + txt_len(t));
            text_cat(t, "ST");
        } else {
            int bb = (v - 64) * 100 / 63;
            if (bb > 0)
                text_cat(t, "+");
            int_text(bb, t + txt_len(t));
        }
        return;
    }
    case K_DEST: text_cat(t, MN_DEST_NAME[ui.dest[d.id]]); return;
    case K_MSEL: int_text(ui.msel[d.id] + 1, t); return;
    case K_MVAL: int_text(v * 100 / 127, t); return;
    case K_SSEL: int_text(ui.ssel + 1, t); return;
    case K_SNOTE: note_text(edit.seq[ui.ssel].note, t); return;
    case K_SGATE:
        if (v == MN_REST)
            text_cat(t, "REST");
        else if (v == MN_TIE)
            text_cat(t, "TIE");
        else {
            int_text(v * 100 / 127, t);
            text_cat(t, "%");
        }
        return;
    case K_SVEL: int_text(v, t); return;
    case K_TEMPO: int_text(edit.bpm, t); return;
    case K_BEND: int_text(set.bend, t); text_cat(t, " ST"); return;
    case K_MCH:
        if (!set.mch)
            text_cat(t, "ALL");
        else
            int_text(set.mch, t);
        return;
    case K_TOUCH: text_cat(t, set.touch ? "ON" : "FIXED"); return;
    default: return;
    }
}

static int32_t accel(int k, int32_t e, int32_t mid, int32_t fast)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_at[k];
    ui.enc_at[k] = now;
    if (dt < 35u)
        return e * fast;
    if (dt < 80u)
        return e * mid;
    return e;
}

static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void knob_turn(int k, int32_t e)
{
    kdef d = kdef_of(k);
    int dir = e > 0 ? 1 : -1, a;
    if (d.kind == K_NONE)
        return;
    ui.knob_at[k] = plat_ms();
    ui.redraw = 1;
    switch (d.kind) {
    case K_PARAM: {
        const mn_param_info *pi = &MN_PARAMS[d.id];
        int v = edit.p[d.id];
        if (pi->kind == MK_LIST)         /* Listen: ein Eintrag pro Raste */
            v = mn_list_value(d.id, mn_list_index(d.id, v) + dir);
        else if (pi->kind == MK_SW)
            v = dir > 0 ? 127 : 0;
        else
            v = iclamp(v + accel(EN_K1 + k, e, 3, 6), 0, 127);
        if (v == edit.p[d.id])
            return;
        edit.p[d.id] = (uint8_t)v;
        if (d.id == P_SLEN && ui.ssel >= mn_seq_len(&eng))
            ui.ssel = (uint8_t)(mn_seq_len(&eng) - 1);
        if (d.id == P_MOD(0) + MO_SCOUNT || d.id == P_MOD(1) + MO_SCOUNT || d.id == P_MOD(2) + MO_SCOUNT) {
            int m = (d.id - P_M1) / MO_N;
            if (ui.msel[m] >= scount(m))
                ui.msel[m] = (uint8_t)(scount(m) - 1);
        }
        break;
    }
    case K_LVL:
        a = iclamp(edit.lvl[d.id][ui.dest[d.id]] + accel(EN_K1 + k, e, 2, 5), 0, 127);
        edit.lvl[d.id][ui.dest[d.id]] = (uint8_t)a;
        break;
    case K_DEST: ui.dest[d.id] = (uint8_t)iclamp(ui.dest[d.id] + dir, 0, MN_NDEST - 1); return;
    case K_MSEL: ui.msel[d.id] = (uint8_t)iclamp(ui.msel[d.id] + dir, 0, scount(d.id) - 1); return;
    case K_MVAL:
        edit.mstep[d.id][ui.msel[d.id]] = (uint8_t)iclamp(edit.mstep[d.id][ui.msel[d.id]] + accel(EN_K1 + k, e, 3, 6), 0, 127);
        break;
    case K_SSEL: ui.ssel = (uint8_t)iclamp(ui.ssel + dir, 0, MN_STEPS - 1); return;
    case K_SNOTE: {
        mn_step *s = &edit.seq[ui.ssel];
        s->note = (uint8_t)iclamp(s->note + accel(EN_K1 + k, e, 1, 3), 24, 96);
        if (s->gate == MN_REST)          /* eine Note auf einer Pause: der Step spielt wieder */
            s->gate = 64;
        break;
    }
    case K_SGATE:
        edit.seq[ui.ssel].gate = (uint8_t)iclamp(edit.seq[ui.ssel].gate + accel(EN_K1 + k, e, 3, 6), 0, 127);
        break;
    case K_SVEL:
        edit.seq[ui.ssel].vel = (uint8_t)iclamp(edit.seq[ui.ssel].vel + accel(EN_K1 + k, e, 3, 6), 1, 127);
        break;
    case K_TEMPO: edit.bpm = (uint16_t)iclamp((int)edit.bpm + accel(EN_K1 + k, e, 2, 5), 30, 300); break;
    case K_BEND:
        set.bend = (uint8_t)iclamp(set.bend + dir, 1, 12);
        eng.bend_range = (float)set.bend;
        dirty();
        return;
    case K_MCH: set.mch = (uint8_t)iclamp(set.mch + dir, 0, 16); dirty(); return;
    case K_TOUCH: set.touch = dir > 0; dirty(); return;
    default: return;
    }
    edited();
}

static void page_set(int pg)
{
    int i;
    for (i = 0; i < 4; i++)              /* eine neue Seite: kein Regler leuchtet noch nach */
        ui.knob_at[i] = 0;
    ui.big_until = 0;
    ui.page = (uint8_t)pg;
    ui.redraw = 1;
    if (ui.rec && pg != PG_SEQ1 && pg != PG_SEQ2) {   /* die SEQ-Seiten verlassen: RECORD endet (wie am Typhon) */
        ui.rec = 0;
    }
    app_say(PAGE_NAME[pg], "");
}

/* ----------------------------------------------------------------- Presets --- */
static void slot_load(int s)
{
    char t[24] = "";
    set.slot = (uint8_t)s;
    edit = *slot_patch(s);
    set.edited = 0;
    mn_changed(&eng);
    dirty();
    t[0] = (char)('0' + (s + 1) / 10);
    t[1] = (char)('0' + (s + 1) % 10);
    t[2] = ' ';
    t[3] = 0;
    text_cat(t, edit.name);
    say_big(edit.name, t);
}

static void slot_save(int s)
{
    int b = s >= 8;
    *slot_patch(s) = edit;
    bank[b].magic = BANK_MAGIC;
    if (plat_store_save(OBJ_BANK_A + (uint32_t)b, &bank[b], sizeof bank[b]) != 0) {
        app_say("SAVE FAILED", "");
        return;
    }
    set.slot = (uint8_t)s;
    set.edited = 0;
    dirty();
    app_say("SAVED", edit.name);
}

static void factory(void)                /* REC 1 s: der Platz zurück auf sein Werks-Preset (gespeichert erst mit SAVE) */
{
    mn_preset(&edit, set.slot);
    set.edited = 1;
    mn_changed(&eng);
    dirty();
    app_say("FACTORY", edit.name);
}

/* -------------------------------------------------------------- Sequencer --- */
static void play_toggle(void)
{
    ui.playing = (uint8_t)!ui.playing;
    e_play(ui.playing);
    dirty_at = plat_ms();
    ui.redraw = 1;
}

static void rec_key(int note, int vel)   /* RECORD: die Taste schreibt den Step und geht weiter (APPEND verlängert) */
{
    mn_step *s = &edit.seq[ui.ssel];
    int len = mn_seq_len(&eng);
    s->note = (uint8_t)iclamp(note, 24, 96);
    s->vel = (uint8_t)iclamp(vel, 1, 127);
    s->gate = 64;                        /* 50 % */
    if (ui.ssel + 1 >= len && len < MN_STEPS) {
        edit.p[P_SLEN] = (uint8_t)mn_list_value(P_SLEN, len);   /* APPEND: eine Stufe länger */
    }
    if (ui.ssel + 1 >= MN_STEPS) {
        ui.rec = 0;
        app_say("RECORD", "32 STEPS: END");
    } else {
        ui.ssel++;
    }
    edited();
}

/* ------------------------------------------------------------------ Eingabe --- */
static int key_base(void) { return 53 + 12 * ((int)set.oct - 3); }   /* Taste 0 = F3 (MIDI 53) bei Oktave 0 */

static void keys_input(uint32_t keys)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int note = key_base() + (int)k, v = set.touch ? touch_vel(k) : 100;
            if (v <= 0)
                v = 100;
            if (note < 12 || note > 115)
                continue;
            ui.key_note[k] = (int8_t)note;
            if (ui.rec) {
                e_on(note, v, 1);
                rec_key(note, v);
            } else {
                e_on(note, v, 0);
            }
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.key_note[k] >= 0)
                e_off(ui.key_note[k]);
            ui.key_note[k] = -1;
        }
    }
}

static void button(uint32_t i)
{
    int seqp = ui.page == PG_SEQ1 || ui.page == PG_SEQ2;
    if (ui.saving && i != B_SAVE) {      /* SAVE abgebrochen */
        ui.saving = 0;
        app_say("SAVE", "CANCELLED");
        return;
    }
    switch (i) {
    case B_HOME: page_set(PG_MAIN); break;
    case B_SEL: page_set(PG_FILTER); break;
    case B_ENV: page_set(ui.page == PG_FENV ? PG_AENV : PG_FENV); break;
    case B_LFO: page_set(ui.page >= PG_M1 && ui.page < PG_M3D ? ui.page + 1 : PG_M1); break;
    case B_EDIT: page_set(ui.page == PG_SET1 ? PG_SET2 : PG_SET1); break;
    case B_FX: page_set(ui.page >= PG_FX1 && ui.page < PG_FXP ? ui.page + 1 : PG_FX1); break;
    case B_GLO: page_set(ui.page == PG_GLO1 ? PG_GLO2 : PG_GLO1); break;
    case B_SEQ: page_set(ui.page == PG_SEQ1 ? PG_SEQ2 : PG_SEQ1); break;
    case B_PLAY: play_toggle(); break;
    case B_REC:
        if (seqp) {
            ui.rec = (uint8_t)!ui.rec;
            ui.redraw = 1;
            app_say(ui.rec ? "RECORD" : "RECORD OFF", ui.rec ? "PLAY THE KEYS" : "");
        } else {
            ui.rec_t = plat_ms();
        }
        break;
    case B_SAVE:
        if (!ui.saving) {
            ui.saving = 1;
            ui.save_slot = set.slot;
            ui.save_until = plat_ms() + 8000u;
            ui.redraw = 1;
        } else {
            ui.saving = 0;
            slot_save(ui.save_slot);
        }
        break;
    case B_ARP:
        if (seqp) {                      /* Step kopieren, dann einfügen */
            if (ui.copied < 0) {
                ui.clip = edit.seq[ui.ssel];
                ui.copied = (int8_t)ui.ssel;
                app_say("COPIED", "STEP");
            } else {
                edit.seq[ui.ssel] = ui.clip;
                ui.copied = -1;
                edited();
                app_say("PASTED", "");
            }
        } else {
            ui.hold = (uint8_t)!ui.hold;
            e_hold(ui.hold);
            app_say("HOLD", ui.hold ? "ON" : "OFF");
        }
        break;
    case B_OCTDN:
    case B_OCTUP: {
        char t[8] = "";
        int o = iclamp((int)set.oct + (i == B_OCTUP ? 1 : -1), 0, 6);
        set.oct = (uint8_t)o;
        dirty();
        if (o > 3)
            text_cat(t, "+");
        int_text(o - 3, t + txt_len(t));
        app_say("OCTAVE", t);
        break;
    }
    default: break;
    }
}

static void held_buttons(uint32_t btn)
{
    if (ui.rec_t) {
        if (!(btn >> B_REC & 1u))
            ui.rec_t = 0;
        else if (plat_ms() - ui.rec_t >= 1000u) {
            ui.rec_t = 0;
            factory();
        }
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    keys_input(keys);
    for (i = 0; i < NB; i++)
        if ((btn >> i & 1u) && !(ui.btn >> i & 1u))
            button(i);
    held_buttons(btn);
    for (i = 0; i < 4u; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0)
            knob_turn((int)i, e);
    if ((e = plat_enc(EN_SELECT)) != 0) {    /* CUTOFF: der große Regler, auf jeder Seite */
        char t[12];
        edit.p[P_CUTOFF] = (uint8_t)iclamp(edit.p[P_CUTOFF] + accel(EN_SELECT, e, 2, 4), 0, 127);
        edited();
        hz_text(mn_cutoff_hz(edit.p[P_CUTOFF]), t);
        say_big(t, "CUTOFF");
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {      /* STORM */
        char t[8];
        edit.p[P_STORM] = (uint8_t)iclamp(edit.p[P_STORM] + accel(EN_ALGO, e, 3, 6), 0, 127);
        edited();
        int_text(edit.p[P_STORM] * 100 / 127, t);
        say_big(t, edit.p[P_STORM] ? "STORM" : "STORM OFF");
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        int s = ((ui.saving ? ui.save_slot : set.slot) + NSLOTS + (e > 0 ? 1 : NSLOTS - 1)) % NSLOTS;
        if (ui.saving) {
            ui.save_slot = (uint8_t)s;
            ui.save_until = plat_ms() + 8000u;
            ui.redraw = 1;
        } else {
            slot_load(s);
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

static void lights(void)
{
    static const uint8_t PAGE_BTN[NPAGES] = {B_HOME, B_SEL, B_ENV, B_ENV, B_LFO, B_LFO, B_LFO, B_LFO, B_LFO, B_LFO,
                                             B_EDIT, B_EDIT, B_FX, B_FX, B_FX, B_FX, B_GLO, B_GLO, B_SEQ, B_SEQ};
    uint32_t btn = 1u << PAGE_BTN[ui.page], dimb, lit = 0, k;
    if (ui.playing)
        btn |= LED_PLAY_GREEN;
    if (ui.rec)
        btn |= 1u << B_REC;
    if (ui.hold)
        btn |= 1u << B_ARP;
    if (ui.saving && (plat_ms() / 250u) & 1u)
        btn |= 1u << B_SAVE;
    if (set.oct != 3)
        btn |= 1u << (set.oct > 3 ? B_OCTUP : B_OCTDN);
    for (k = 0; k < NKEYS; k++)          /* C-Tasten glimmen: Orientierung */
        if ((key_base() + (int)k) % 12 == 0)
            lit |= 1u << k;
    dimb = (1u << NB) - 1u;
    plat_lights(btn | (ui.btn & ~(1u << B_PLAY)), ui.keys, dimb & ~(1u << B_PLAY), lit, 1);
}

/* ----------------------------------------------------------------- Zeichnen --- */
/* Die Optik des Typhon: ein anthrazitfarbenes Panel, der Name in Gelb, schwarze Regler mit Silberkappe und einem
 * türkisen Bogen, unten das OLED in seinem gelben Rahmen (weiß auf schwarz). */
#undef C_PANEL
#define C_PANEL RGB(26, 26, 28)
#undef C_YEL
#define C_YEL RGB(250, 212, 30)
#undef C_YEL_DIM
#define C_YEL_DIM RGB(110, 92, 16)
#undef C_TEAL
#define C_TEAL RGB(110, 206, 202)
#undef C_TEAL_DIM
#define C_TEAL_DIM RGB(36, 66, 66)
#undef C_WHITE
#define C_WHITE RGB(236, 236, 236)
#undef C_SUB
#define C_SUB RGB(140, 140, 146)
#undef C_LINE
#define C_LINE RGB(54, 54, 58)
#undef C_KNOB
#define C_KNOB RGB(16, 16, 18)
#undef C_KEDGE
#define C_KEDGE RGB(70, 70, 74)
#undef C_SILVER
#define C_SILVER RGB(176, 176, 182)
#undef C_SILVER_HI
#define C_SILVER_HI RGB(236, 236, 240)
#undef C_OLED
#define C_OLED RGB(0, 0, 0)
#undef C_OLED_DIM
#define C_OLED_DIM RGB(70, 70, 74)
#undef C_RED
#define C_RED RGB(232, 70, 56)

static void ctext(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t dy;
    for (dy = -r; dy <= r; dy++) {
        int32_t w = 0;
        while ((w + 1) * (w + 1) + dy * dy <= r * r)
            w++;
        cv_rect(cx - w, cy + dy, 2 * w + 1, 1, c);
    }
}

/* ein Bogen von a0 bis a1 (Grad, 0 = oben, im Uhrzeigersinn), Dicke th */
static void arc(int32_t cx, int32_t cy, int32_t r, float a0, float a1, int32_t th, uint16_t c)
{
    float a, step;
    if (a1 < a0) {
        float t = a0;
        a0 = a1;
        a1 = t;
    }
    step = 180.0f / (3.14159f * (float)r) * 0.8f;
    for (a = a0; a <= a1 + 0.01f; a += step) {
        float rad = a * 0.017453293f, sn = fm_sinf(rad), cs = fm_cosf(rad);
        int32_t t;
        for (t = 0; t < th; t++)
            cv_rect(cx + (int32_t)(sn * (float)(r - t) + (sn < 0 ? -0.5f : 0.5f)),
                    cy - (int32_t)(cs * (float)(r - t) + (cs < 0 ? -0.5f : 0.5f)), 1, 1, c);
    }
}

/* ein Typhon-Regler: der türkise Ring (dunkel die Spur, hell der Wert), schwarzer Körper, Silberkappe, Zeiger */
static void typhon_knob(int32_t cx, int32_t cy, int32_t r, float v, int hot, int bip)
{
    float a = -135.0f + 270.0f * fm_clampf(v, 0.0f, 1.0f), sn, cs;
    int32_t i, cap = r * 10 / 20;
    arc(cx, cy, r + 6, -135.0f, 135.0f, 3, C_TEAL_DIM);
    arc(cx, cy, r + 6, bip ? 0.0f : -135.0f, a, 3, hot ? C_YEL : C_TEAL);
    disc(cx, cy, r, hot ? C_SUB : C_KEDGE);
    disc(cx, cy, r - 1, C_KNOB);
    disc(cx, cy, cap, C_SILVER);
    disc(cx - cap / 3, cy - cap / 3, cap / 2, C_SILVER_HI);
    sn = fm_sinf(a * 0.017453293f);
    cs = fm_cosf(a * 0.017453293f);
    for (i = cap - 2; i < r - 1; i++)
        cv_rect(cx + (int32_t)(sn * (float)i), cy - (int32_t)(cs * (float)i), 2, 2, C_WHITE);
}

static void draw_knob(int k, int32_t cx, int32_t y, uint32_t now)
{
    kdef d = kdef_of(k);
    int v = knob_get(d), hot = ui.knob_at[k] && now - ui.knob_at[k] < 600u;
    char t[20];
    if (v < 0 || d.kind == K_NONE)
        return;
    ctext(cx, y, &FONT_XS, knob_label(d), hot ? C_YEL : C_WHITE);
    typhon_knob(cx, y + 38, 15, (float)v / 127.0f, hot, knob_bip(d));
    val_text(d, t);
    ctext(cx, y + 64, &FONT_XS, t, hot ? C_YEL : C_SUB);
}

/* ---- das OLED unten: was zur Seite gehört ---- */
static void oled_frame(void)
{
    cv_rect(4, 2, 232, 96, C_YEL);
    cv_rect(6, 4, 228, 92, C_OLED);
}

static void env_curve(int32_t x0, int32_t y0, int32_t w, int32_t h, int a, int dd, int s, int r, int tm, uint16_t c)
{
    /* die Stufen auf eine gemeinsame Zeitachse (gestaucht), S als Plateau */
    float sc = tm < 64 ? fm_exp2f(-((float)tm - 64.0f) / 63.0f * 3.3219f) : fm_exp2f(-((float)tm - 64.0f) / 63.0f * 2.585f);
    float ta = mn_env_ms(a, 1) * sc, td = mn_env_ms(dd, 0) * sc, tr = mn_env_ms(r, 0) * sc, ts = 300.0f;
    float wa, wd, ws, wr, tot, sl = (float)s / 127.0f;
    int32_t xa, xd, xs, xr, ys = y0 + h - (int32_t)(sl * (float)h);
    wa = fm_sqrtf(ta);
    wd = fm_sqrtf(td);
    ws = fm_sqrtf(ts);
    wr = fm_sqrtf(tr);
    tot = wa + wd + ws + wr;
    xa = x0 + (int32_t)(wa / tot * (float)w);
    xd = xa + (int32_t)(wd / tot * (float)w);
    xs = xd + (int32_t)(ws / tot * (float)w);
    xr = x0 + w;
    cv_line(x0, y0 + h, xa, y0, c);
    {   /* Abfall (exponentiell angenähert) */
        int32_t i, px = xa, py = y0;
        for (i = 1; i <= 12; i++) {
            float f = (float)i / 12.0f, lv = sl + (1.0f - sl) * fm_exp2f(-f * 6.0f);
            int32_t nx = xa + (int32_t)((float)(xd - xa) * f), ny = y0 + h - (int32_t)(lv * (float)h);
            cv_line(px, py, nx, ny, c);
            px = nx;
            py = ny;
        }
        cv_line(px, py, xs, ys, c);
        px = xs;
        py = ys;
        for (i = 1; i <= 12; i++) {
            float f = (float)i / 12.0f, lv = sl * fm_exp2f(-f * 6.0f);
            int32_t nx = xs + (int32_t)((float)(xr - xs) * f), ny = y0 + h - (int32_t)(lv * (float)h);
            cv_line(px, py, nx, ny, c);
            px = nx;
            py = ny;
        }
    }
    (void)xr;
}

static void oled_wave(void)              /* die Wellenform der WAVE-Stellung (zwei Perioden) */
{
    int32_t x, px = 0, py = 0;
    int wi = mn_wave_index(edit.p[P_WAVE]);
    for (x = 0; x < 200; x++) {
        float ph = (float)x / 100.0f, y;
        float p1 = ph - (float)(int)ph;
        float t2 = mn_tune2_semis(edit.p[P_TUNE2]), r2 = fm_exp2f(t2 / 12.0f), p2 = ph * r2;
        p2 -= (float)(int)p2;
        switch (wi) {
        case 0: y = 1.0f - 4.0f * fabsf_(p1 - 0.5f); break;
        case 1: y = 2.0f * p1 - 1.0f; break;
        case 2: y = (2.0f * p1 - 1.0f + 2.0f * p2 - 1.0f) * 0.5f; break;
        case 3: y = (2.0f * p1 - 1.0f + (p2 < 0.5f ? 1.0f : -1.0f)) * 0.5f; break;
        case 4: y = ((p1 < 0.5f ? 1.0f : -1.0f) + (p2 < 0.5f ? 1.0f : -1.0f)) * 0.5f; break;
        case 5: y = p1 < 0.16f ? 1.0f : -0.6f; break;
        default: {
            float m = 1.0f - 4.0f * fabsf_(p2 - 0.5f), q = p1 + (wi == 6 ? 0.12f : 0.35f) * m;
            q -= (float)(int)q;
            if (q < 0.0f)
                q += 1.0f;
            y = 1.0f - 4.0f * fabsf_(q - 0.5f);
            break;
        }
        }
        {
            int32_t nx = 20 + x, ny = 50 - (int32_t)(y * 26.0f);
            if (x)
                cv_line(px, py, nx, ny, C_WHITE);
            px = nx;
            py = ny;
        }
    }
    cv_text(12, 8, &FONT_XS, "WAVE", C_OLED_DIM);
    cv_text(50, 8, &FONT_XS, MN_WAVE_NAME[wi], C_WHITE);
}

static void oled_filter(void)            /* die Filterkurve: flach bis zum Cutoff, die Resonanz als Spitze */
{
    float fc = (float)edit.p[P_CUTOFF] / 127.0f, q = (float)edit.p[P_RESON] / 127.0f;
    float hp = edit.p[P_HPCUT] ? 0.43f + (float)edit.p[P_HPCUT] / 127.0f : 0.0f;
    int32_t x, px = 0, py = 0;
    for (x = 0; x <= 210; x++) {
        float f = (float)x / 210.0f, d = (f - fc) * 10.0f, db;
        if (d < 0.0f)
            db = q * 18.0f * fm_exp2f(d * 3.0f);
        else
            db = q * 18.0f * fm_exp2f(-d * 6.0f) - d * 24.0f;
        if (hp > 0.0f && f < hp)
            db -= (hp - f) * 120.0f;
        {
            int32_t nx = 15 + x, ny = 46 - (int32_t)(db * 1.1f);
            ny = ny < 8 ? 8 : ny > 90 ? 90 : ny;
            if (x)
                cv_line(px, py, nx, ny, C_WHITE);
            px = nx;
            py = ny;
        }
    }
    cv_text(12, 8, &FONT_XS, "VCF 24DB LP", C_OLED_DIM);
}

static void oled_env(int filt)
{
    if (filt)
        env_curve(16, 18, 208, 66, edit.p[P_FA], edit.p[P_FD], edit.p[P_FS], edit.p[P_FR], edit.p[P_FTIME], C_WHITE);
    else
        env_curve(16, 18, 208, 66, edit.p[P_AA], edit.p[P_AD], edit.p[P_AS], edit.p[P_AR], edit.p[P_ATIME], C_WHITE);
    cv_text(12, 8, &FONT_XS, filt ? "FILTER ADSR" : "AMP ADSR", C_OLED_DIM);
}

static void oled_mod(int m, int dest_page)
{
    int mode = mode_of(m), i, n = 0;
    char t[24] = "M";
    t[1] = (char)('1' + m);
    t[2] = 0;
    text_cat(t, " ");
    text_cat(t, MN_MODE_NAME[mode]);
    cv_text(12, 8, &FONT_XS, t, C_WHITE);
    if (dest_page) {                     /* die Ziele mit ihrem LVL als Balken (das gewählte hell) */
        for (i = 0; i < MN_NDEST; i++) {
            int v = edit.lvl[m][i], sel = i == ui.dest[m];
            int32_t x = 12 + (i % 2) * 112, y = 20 + (i / 2) * 10, w;
            cv_text(x, y, &FONT_XS, MN_DEST_NAME[i], sel ? C_YEL : v != 64 ? C_WHITE : C_OLED_DIM);
            cv_rect(x + 34, y + 5, 64, 1, C_OLED_DIM);
            w = (v - 64) * 32 / 63;
            if (w > 0)
                cv_rect(x + 66, y + 3, w, 5, sel ? C_YEL : C_TEAL);
            else if (w < 0)
                cv_rect(x + 66 + w, y + 3, -w, 5, sel ? C_YEL : C_TEAL);
            n += v != 64;
        }
        (void)n;
        return;
    }
    if (mode == MODE_STEP) {             /* die Step-Werte als Säulen */
        int cnt = scount(m), cur = mn_mod_step(&eng, m) % cnt;
        for (i = 0; i < cnt; i++) {
            int32_t w = 200 / cnt, x = 16 + i * w, h = edit.mstep[m][i] * 60 / 127;
            uint16_t c = i == ui.msel[m] ? C_YEL : i == cur ? C_TEAL : C_WHITE;
            cv_rect(x + 1, 86 - h, w - 3, h + 1, c);
        }
        return;
    }
    {   /* LFO, Hüllkurve, Zufall: der Ausgang über die Zeit (ein Oszilloskop) */
        int32_t x, px = 0, py = 0;
        cv_rect(12, 54, 216, 1, C_OLED_DIM);
        for (x = 0; x < 96; x++) {
            float v = ui.scope[m][(ui.scope_pos + x) % 96u];
            int32_t nx = 14 + x * 22 / 10, ny = 54 - (int32_t)(v * 30.0f);
            if (x)
                cv_line(px, py, nx, ny, C_WHITE);
            px = nx;
            py = ny;
        }
    }
}

static void oled_fx(void)                /* die Kette FX1 -> FX2 -> FX3, die Seite hell, die Mischung als Balken */
{
    static const char *const T[3] = {"FX1", "FX2", "FX3"};
    int i;
    for (i = 0; i < 3; i++) {
        int32_t x = 12 + i * 76;
        int on = ui.page == PG_FX1 + i || ui.page == PG_FXP;
        int mix = edit.p[i == 0 ? P_X1MIX : i == 1 ? P_X2MIX : P_X3MIX];
        const char *nm = i == 0 ? MN_X1_NAME[mn_list_index(P_X1TYPE, edit.p[P_X1TYPE])]
                         : i == 1 ? MN_X2_NAME[mn_list_index(P_X2TYPE, edit.p[P_X2TYPE])]
                                  : MN_X3_NAME[mn_list_index(P_X3TYPE, edit.p[P_X3TYPE])];
        cv_rect(x, 20, 66, 46, on ? C_WHITE : C_OLED_DIM);
        cv_rect(x + 1, 21, 64, 44, C_OLED);
        ctext(x + 33, 25, &FONT_XS, T[i], on ? C_YEL : C_OLED_DIM);
        ctext(x + 33, 42, &FONT_XS, nm, mix ? C_WHITE : C_OLED_DIM);
        cv_rect(x + 4, 74, 58, 4, C_OLED_DIM);
        cv_rect(x + 4, 74, mix * 58 / 127, 4, C_TEAL);
        if (i < 2)
            cv_line(x + 67, 43, x + 75, 43, C_WHITE);
    }
    cv_text(12, 84, &FONT_XS, "MIX", C_OLED_DIM);
}

static void oled_seq(void)               /* 32 Steps in zwei Reihen: Note als Höhe, Pause leer, TIE verbunden */
{
    int i, len = mn_seq_len(&eng), cur = mn_step_now(&eng);
    char t[28] = "";
    for (i = 0; i < MN_STEPS; i++) {
        const mn_step *s = &edit.seq[i];
        int32_t x = 12 + (i % 16) * 13, y = 8 + (i / 16) * 34;
        int in = i < len;
        uint16_t c = !in ? C_OLED_DIM : i == cur ? C_TEAL : C_WHITE;
        if (i == ui.ssel)
            cv_rect(x - 1, y - 1, 13, 30, C_YEL);
        cv_rect(x, y, 11, 28, C_OLED);
        if (s->gate == MN_REST) {
            cv_rect(x + 3, y + 24, 5, 1, c);
        } else {
            int32_t h = 3 + (s->note - 24) * 22 / 72;
            int32_t w = s->gate == MN_TIE ? 13 : 2 + s->gate * 8 / 127;
            cv_rect(x, y + 26 - h, w > 11 && i % 16 == 15 ? 11 : w, 3, c);
        }
    }
    note_text(edit.seq[ui.ssel].note, t);
    cv_text(12, 80, &FONT_XS, "STEP", C_OLED_DIM);
    {
        char n[4];
        int_text(ui.ssel + 1, n);
        cv_text(44, 80, &FONT_XS, n, C_WHITE);
    }
    cv_text(66, 80, &FONT_XS, edit.seq[ui.ssel].gate == MN_REST ? "REST" : t, C_WHITE);
    if (ui.rec)
        cv_text(228 - text_w(&FONT_XS, ui.ssel + 1 >= len ? "APPEND" : "RECORD"), 80, &FONT_XS,
                ui.ssel + 1 >= len ? "APPEND" : "RECORD", C_RED);
    else if (ui.copied >= 0)
        cv_text(228 - text_w(&FONT_XS, "COPIED"), 80, &FONT_XS, "COPIED", C_YEL);
}

static void oled_glo(void)
{
    char t[32] = "BPM ";
    int_text((int32_t)(mn_bpm(&eng) + 0.5f), t + 4);
    if (eng.ext)
        text_cat(t, "  MIDI CLOCK");
    cv_text(12, 8, &FONT_XS, t, C_WHITE);
    cv_text(12, 26, &FONT_XS, "KEYS", C_OLED_DIM);
    {
        char n[12] = "";
        note_text(key_base(), n);
        text_cat(n, " - ");
        note_text(key_base() + NKEYS - 1, n + txt_len(n));
        cv_text(60, 26, &FONT_XS, n, C_WHITE);
    }
    cv_text(12, 44, &FONT_XS, "HOLD", C_OLED_DIM);
    cv_text(60, 44, &FONT_XS, ui.hold ? "ON" : "OFF", C_WHITE);
    {
        char c[24] = "CPU ";
        int_text((int32_t)plat_cpu_pct(), c + 4);
        text_cat(c, "%");
        if (eng.eco)
            text_cat(c, " ECO");
        cv_text(12, 62, &FONT_XS, c, plat_cpu_pct() > 80u ? C_RED : C_WHITE);
    }
    cv_text(12, 80, &FONT_XS, "MONSUN V" APP_VERSION, C_OLED_DIM);
}

static void draw_oled(void)
{
    int m = mod_of_page(ui.page);
    oled_frame();
    if (ui.saving) {                     /* SAVE: der Zielplatz */
        char t[24] = "TO ";
        const mn_patch *p = slot_patch(ui.save_slot);
        t[3] = (char)('0' + (ui.save_slot + 1) / 10);
        t[4] = (char)('0' + (ui.save_slot + 1) % 10);
        t[5] = 0;
        ctext(120, 14, &FONT_B, "SAVE", C_YEL);
        ctext(120, 38, &FONT_B, t, C_WHITE);
        ctext(120, 60, &FONT_XS, p->name, C_OLED_DIM);
        ctext(120, 80, &FONT_XS, "PRESET: SLOT   SAVE: OK", C_OLED_DIM);
        return;
    }
    if (m >= 0) {
        oled_mod(m, (ui.page - PG_M1) & 1);
        return;
    }
    switch (ui.page) {
    case PG_MAIN: oled_wave(); break;
    case PG_FILTER: oled_filter(); break;
    case PG_FENV: oled_env(1); break;
    case PG_AENV: oled_env(0); break;
    case PG_SET1:
    case PG_SET2: oled_env(0); break;
    case PG_FX1:
    case PG_FX2:
    case PG_FX3:
    case PG_FXP: oled_fx(); break;
    case PG_SEQ1:
    case PG_SEQ2: oled_seq(); break;
    default: oled_glo(); break;
    }
}

static void draw(void)
{
    uint32_t now = plat_ms();
    char t[24];
    /* Kopf: der Name in Gelb wie am Panel, der Platz, Tempo, Transport; oder eine Meldung */
    cv_begin(240, 30, C_PANEL);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_YEL);
    } else {
        int32_t x = cv_text(8, 3, &FONT_B, "MONSUN", C_YEL);
        cv_text(x + 6, 4, &FONT_XS, "MONO", C_WHITE);
        cv_text(x + 6, 14, &FONT_XS, "SYNTH", C_WHITE);
        t[0] = (char)('0' + (set.slot + 1) / 10);
        t[1] = (char)('0' + (set.slot + 1) % 10);
        t[2] = set.edited ? '*' : 0;
        t[3] = 0;
        cv_text(132, 9, &FONT_XS, t, set.edited ? C_YEL : C_WHITE);
        t[0] = 0;
        int_text((int32_t)(mn_bpm(&eng) + 0.5f), t);
        cv_text(206 - text_w(&FONT_XS, t), 9, &FONT_XS, t, eng.ext ? C_TEAL : C_WHITE);
        if (ui.playing) {
            int32_t i;
            for (i = 0; i < 6; i++)
                cv_rect(214 + i, 9 + i, 1, 12 - 2 * i, C_TEAL);
        } else {
            cv_rect(213, 10, 10, 10, C_SUB);
        }
        if (edit.p[P_STORM]) {           /* STORM an: ein kleiner Wirbel */
            int32_t r = 2 + edit.p[P_STORM] * 5 / 127;
            arc(160, 15, r + 2, 0.0f, 300.0f, 1, C_TEAL);
            arc(160, 15, r, 120.0f, 400.0f, 1, C_TEAL);
        }
    }
    cv_rect(4, 29, 232, 1, C_LINE);
    cv_blit(0, 0);
    /* die Mitte: die Seite und ihre vier Regler (oder groß: CUTOFF / STORM / ein Preset) */
    cv_begin(240, 110, C_PANEL);
    {
        int32_t x = cv_text(8, 3, &FONT_XS, PAGE_NAME[ui.page], C_YEL);
        cv_text(x + 8, 3, &FONT_XS, edit.name, C_SUB);
        {
            int32_t i, bx = 232 - 4 * 9;   /* die vier Regler: welche Seite */
            int m = mod_of_page(ui.page);
            if (m >= 0)
                for (i = 0; i < 3; i++)
                    cv_rect(bx + i * 12, 6, 8, 4, i == m ? C_YEL : C_LINE);
        }
    }
    if (now < ui.big_until) {
        ctext(120, 24, &FONT_XS, ui.big[1], C_SUB);
        ctext(120, 42, &APP_BIG_FONT, ui.big[0], C_YEL);
    } else {
        int i;
        for (i = 0; i < 4; i++)
            draw_knob(i, 33 + i * 58, 16, now);
    }
    cv_blit(0, 30);
    /* unten: das OLED */
    cv_begin(240, 100, C_PANEL);
    draw_oled();
    cv_blit(0, 140);
}

/* ------------------------------------------------------------ der Vertrag --- */
static int16_t fx_dl[MN_DLY_FRAMES] __attribute__((section(".pool")));
static int16_t fx_dr[MN_DLY_FRAMES] __attribute__((section(".pool")));
static float fx_cho[MN_CHO_FRAMES] __attribute__((section(".pool")));
static float fx_rev[MN_REV_FLOATS] __attribute__((section(".pool")));
static float fx_wide[MN_WIDE_FRAMES] __attribute__((section(".pool")));

void app_init(void)
{
    int i;
    bank_load(0);
    bank_load(1);
    settings_load();
    mn_init(&eng, SR, &edit);
    mn_fx_memory(&eng, fx_dl, fx_dr, fx_cho, fx_rev, fx_wide);
    eng.bend_range = (float)set.bend;
    for (i = 0; i < NKEYS; i++)
        ui.key_note[i] = -1;
    ui.copied = -1;
}

static void midi_in(void)                /* im Audio-Interrupt: Noten, Pitch Bend, Clock, Transport */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ty = st & 0xF0u;
        switch (st) {
        case 0xF8u: mn_midi_clock(&eng); continue;
        case 0xFAu: mn_play(&eng, 1); ui.playing = 1; continue;
        case 0xFBu: mn_continue(&eng); ui.playing = 1; continue;
        case 0xFCu: mn_play(&eng, 0); ui.playing = 0; continue;
        default: break;
        }
        if (st >= 0xF0u || (set.mch && (st & 0x0Fu) != set.mch - 1u))
            continue;
        if (ty == 0x90u && d2)
            mn_note_on(&eng, (int)d1, (int)d2);
        else if (ty == 0x80u || ty == 0x90u)
            mn_note_off(&eng, (int)d1);
        else if (ty == 0xE0u)
            mn_bend(&eng, (int)(d2 << 7 | d1));
        else if (ty == 0xB0u && (d1 == 123u || d1 == 120u))
            mn_all_off(&eng);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float bl[256], br[256];
    float gain = (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f;
    uint32_t i;
    if (n > 256u)
        n = 256u;
    midi_in();
    mn_render(&eng, bl, br, (int)n);
    for (i = 0; i < n; i++) {
        lr[2 * i] = (int32_t)(bl[i] * gain);
        lr[2 * i + 1] = (int32_t)(br[i] * gain);
    }
}

void app_panic(void)
{
    ui.playing = 0;
    mn_play(&eng, 0);
    mn_all_off(&eng);
}

void app_splash(void)                    /* MONSUN in Gelb, ein türkiser Wirbel, wie das Panel */
{
    int32_t i;
    lcd_fill(0, 0, 240, 240, C_PANEL);
    cv_begin(240, 110, C_PANEL);
    for (i = 0; i < 5; i++)              /* der Wirbel: Bögen, die sich nach innen drehen */
        arc(120, 56, 48 - i * 9, (float)(i * 70), (float)(i * 70 + 250), 3, i & 1 ? C_TEAL_DIM : C_TEAL);
    disc(120, 56, 5, C_YEL);
    cv_blit(0, 24);
    cv_begin(240, 106, C_PANEL);
    ctext(120, 8, &APP_BIG_FONT, "MONSUN", C_YEL);
    ctext(120, 52, &FONT_XS, "MONO SYNTHESIZER", C_WHITE);
    cv_rect(70, 70, 100, 1, C_SUB);
    ctext(120, 80, &FONT_XS, "by swtdr34mz", C_SUB);
    cv_blit(0, 134);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_PANEL);
    touch_on(1);
    ui.step_drawn = -2;
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    int32_t s, lv, m;
    input();
    lights();
    if (ann_on() && ann_where()) {
        char t[48] = "MONSUN. ";
        text_cat(t, PAGE_NAME[ui.page]);
        text_cat(t, ". ");
        text_cat(t, edit.name);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    ui.playing = (uint8_t)eng.running;
    if (ui.saving && now > ui.save_until) {
        ui.saving = 0;
        ui.redraw = 1;
    }
    for (m = 0; m < 3; m++)              /* das Oszilloskop der Modulatoren */
        ui.scope[m][ui.scope_pos] = mn_mod_out(&eng, m);
    ui.scope_pos = (uint8_t)((ui.scope_pos + 1u) % 96u);
    if (mod_of_page(ui.page) >= 0)
        ui.redraw = 1;
    s = mn_step_now(&eng);
    lv = (int32_t)(eng.level_now * 8.0f);
    if (s != ui.step_drawn || lv != ui.lvl_drawn) {
        ui.step_drawn = s;
        ui.lvl_drawn = lv;
        if (ui.page == PG_SEQ1 || ui.page == PG_SEQ2)
            ui.redraw = 1;
    }
    if ((ui.msg_until && now >= ui.msg_until) || (ui.big_until && now >= ui.big_until)) {
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        if (now >= ui.big_until)
            ui.big_until = 0;
        ui.redraw = 1;
    }
    if (now - ui.load_at >= 500u) {
        uint32_t l = plat_cpu_pct() | plat_xruns() << 8;
        ui.load_at = now;
        if (l != ui.load_shown) {
            ui.load_shown = l;
            if (ui.page == PG_GLO1 || ui.page == PG_GLO2)
                ui.redraw = 1;
        }
        /* bleibt die Last eine Sekunde über 88 %, rechnet die Stimme ohne Oversampling (ECO) weiter */
        if (plat_cpu_pct() > 88u && !eng.eco) {
            if (++ui.hot >= 2) {
                eng.eco = 1;
                app_say("ECO MODE", "CPU HIGH");
            }
        } else {
            ui.hot = 0;
        }
    }
    /* gespeichert nur, wenn der Sequencer steht: Flash schreiben hält den Audio-Interrupt an */
    if (!ui.playing && set_dirty && now - dirty_at > 1500u)
        settings_save();
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = ui.playing;
    else if (!strcmp(what, "step"))
        *got = mn_step_now(&eng);
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "slot"))
        *got = set.slot;
    else if (!strcmp(what, "edited"))
        *got = set.edited;
    else if (!strcmp(what, "bpm"))
        *got = edit.bpm;
    else if (!strcmp(what, "len"))
        *got = mn_seq_len(&eng);
    else if (!strcmp(what, "rec"))
        *got = ui.rec;
    else if (!strcmp(what, "ssel"))
        *got = ui.ssel;
    else if (!strcmp(what, "note"))
        *got = eng.note;
    else if (!strcmp(what, "gate"))
        *got = eng.gate;
    else if (!strcmp(what, "hold"))
        *got = ui.hold;
    else if (!strcmp(what, "oct"))
        *got = set.oct;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "dest"))
        *got = ui.dest[0];
    else if (!strncmp(what, "seqnote", 7))
        *got = edit.seq[atoi(what + 7) - 1].note;
    else if (!strncmp(what, "lvl", 3))   /* lvlMD: Modulator M, Ziel D */
        *got = edit.lvl[what[3] - '1'][atoi(what + 4)];
    else if (what[0] == 'p' && what[1] >= '0' && what[1] <= '9')   /* pN: Parameter N */
        *got = edit.p[atoi(what + 1)];
    else
        return 0;
    return 1;
}
#endif
