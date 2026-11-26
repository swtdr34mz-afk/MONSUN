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

enum { R_HEAD = 1, R_MID = 2, R_OLED = 4, R_ALL = 7 };   /* was neu gezeichnet wird */

static struct {
    char msg[2][24];
    uint32_t msg_until;
    char big[2][20];
    uint32_t big_until;
    uint32_t knob_at[4], enc_at[NE];
    uint32_t keys, btn;
    uint8_t page, playing, hold, redraw;
    uint8_t intro;                       /* die Eröffnung läuft */
    uint32_t intro_at, frame;
    int32_t peak_drawn;
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
    ui.redraw |= R_ALL;
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
    ui.redraw |= R_ALL;
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
    ui.redraw |= R_ALL;
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
    ui.redraw |= R_ALL;
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
    ui.redraw |= R_ALL;
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
            ui.redraw |= R_ALL;
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
            ui.redraw |= R_ALL;
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
            ui.redraw |= R_ALL;
        } else {
            slot_load(s);
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw |= R_ALL;
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
/* Die Optik des Typhon, veredelt: ein Panel aus eloxiertem Aluminium in Anthrazit, der Name in leuchtendem Gelb,
 * Regler mit gerändeltem Rock und gebürsteter Alukappe auf einem leuchtenden türkisen Ring, und unten das OLED in
 * seinem gelben Rahmen, hinter Glas, mit lebendigen, leuchtenden Kurven (das echte Ausgangssignal, der Cutoff, die
 * Hüllkurven und Modulatoren, wie sie gerade laufen). Alles kantengeglättet (ui_gfx.h). */
#undef C_PANEL
#define C_PANEL RGB(26, 26, 28)
#undef C_YEL
#define C_YEL RGB(255, 212, 32)
#undef C_YEL_DIM
#define C_YEL_DIM RGB(96, 76, 10)
#undef C_TEAL
#define C_TEAL RGB(96, 224, 216)
#undef C_TEAL_DIM
#define C_TEAL_DIM RGB(30, 62, 62)
#undef C_WHITE
#define C_WHITE RGB(238, 238, 240)
#undef C_SUB
#define C_SUB RGB(130, 132, 138)
#undef C_LINE
#define C_LINE RGB(54, 54, 58)
#undef C_OLED_DIM
#define C_OLED_DIM RGB(78, 82, 86)
#undef C_RED
#define C_RED RGB(255, 72, 56)

#include "ui_gfx.h"

/* das Ausgangssignal fürs Oszilloskop (der Audio-Interrupt schreibt, die Anzeige liest) */
#define SCOPE_N 1024u
static float scope_ring[SCOPE_N];
static volatile uint32_t scope_w;
static volatile float out_peak;

static void ctext(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

/* ein abgerundetes Rechteck: senkrechter Verlauf, glatte Ecken */
static void rrect(int32_t x, int32_t y, int32_t w, int32_t h, float rad, col3 top, col3 bot, float a)
{
    int32_t i, j;
    for (j = 0; j < h; j++) {
        col3 c = mix3(top, bot, h > 1 ? (float)j / (float)(h - 1) : 0.0f);
        float py = (float)j + 0.5f, cy = py < rad ? rad : py > (float)h - rad ? (float)h - rad : py;
        if (!px_ok(0, y + j))
            continue;
        for (i = 0; i < w; i++) {
            float px = (float)i + 0.5f, cx = px < rad ? rad : px > (float)w - rad ? (float)w - rad : px, cov = 1.0f;
            if (cx != px && cy != py) {
                float dx = px - cx, dy = py - cy;
                cov = sat01(rad - fm_sqrtf(dx * dx + dy * dy) + 0.5f);
            }
            if (cov * a >= 0.996f) {
                if (px_ok(x + i, y + j))
                    px_put(x + i, y + j, c);
            } else {
                px_blend(x + i, y + j, c, cov * a);
            }
        }
    }
}

/* eine Linie mit weißem Kern und farbigem Schein */
static void neon(float x0, float y0, float x1, float y1, col3 halo, float a)
{
    aa_line(x0, y0 - 1.0f, x1, y1 - 1.0f, halo, a * 0.35f);
    aa_line(x0, y0 + 1.0f, x1, y1 + 1.0f, halo, a * 0.35f);
    aa_line(x0, y0 - 2.0f, x1, y1 - 2.0f, halo, a * 0.10f);
    aa_line(x0, y0 + 2.0f, x1, y1 + 2.0f, halo, a * 0.10f);
    aa_line(x0, y0, x1, y1, mix3(halo, rgb3(255, 255, 255), 0.65f), a);
}

/* eine Fläche unter einer Kurve: von oben (deutlich) nach unten (verschwindend) */
static void fill_col(int32_t x, float ytop, float ybase, col3 c, float a)
{
    int32_t y, y0 = (int32_t)(ytop < ybase ? ytop : ybase), y1 = (int32_t)(ytop < ybase ? ybase : ytop);
    float span = (float)(y1 - y0) + 1.0f;
    for (y = y0; y <= y1; y++)
        px_blend(x, y, c, a * (1.0f - (float)(y - y0) / span * 0.85f));
}

/* ---- der Regler ---- */
static void knob_pro(float cx, float cy, float r, float v, int hot, int bip)
{
    float va = -135.0f + 270.0f * sat01(v), rr = r + 7.0f, a0 = bip ? 0.0f : -135.0f, rc = r * 0.62f;
    float ux = fm_sinf(va * 0.0174533f), uy = -fm_cosf(va * 0.0174533f);
    col3 acc = c3(hot ? C_YEL : C_TEAL);
    int32_t x, y, R = (int32_t)(r + 5.0f);
    /* der Ring: die Spur, der Wert mit Schein, ein Lichtpunkt am Ende */
    ring_arc(cx, cy, rr, 3.0f, -135.0f, 135.0f, c3(C_TEAL_DIM), 1.0f, 0.0f);
    if (fabsf_(va - a0) > 0.8f)
        ring_arc(cx, cy, rr, 3.0f, va < a0 ? va : a0, va < a0 ? a0 : va, acc, 1.0f, hot ? 6.0f : 4.0f);
    bloom(cx + ux * rr, cy + uy * rr, hot ? 7.0f : 5.0f, acc, hot ? 0.9f : 0.6f);
    aa_disc(cx + ux * rr, cy + uy * rr, 1.6f, rgb3(255, 255, 255), 0.95f);
    /* Schatten, Rock, Kappe, Zeiger: ein Durchgang */
    for (y = (int32_t)cy - R; y <= (int32_t)cy + R; y++)
        for (x = (int32_t)cx - R; x <= (int32_t)cx + R; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            float sdx = dx - 1.5f, sdy = dy - 2.5f, sd = fm_sqrtf(sdx * sdx + sdy * sdy), cov, an, lam, proj, perp;
            col3 c;
            if (!px_ok(x, y))
                continue;
            if (sd < r + 3.0f)               /* der Schatten nach rechts unten */
                px_blend(x, y, rgb3(0, 0, 0), sat01((r + 3.0f - sd) / 4.5f) * 0.55f);
            if (d > r + 0.5f)
                continue;
            cov = sat01(r - d + 0.5f);
            an = ang_deg(dx, dy);
            lam = d > 0.5f ? -(dx + dy) * 0.7071f / d : 0.0f;   /* Licht von links oben */
            if (d > rc) {                    /* der Rock: gerändelt, oben heller */
                float kn = fm_cosf(an * 0.0174533f * 28.0f), edge = sat01((d - (r - 1.6f)) / 1.6f);
                float g = 24.0f + 16.0f * lam + 7.0f * kn - 10.0f * edge + 26.0f * sat01(1.0f - (d - rc) / 1.4f) * sat01(lam + 0.3f);
                c = rgb3(g, g, g + 2.0f);
            } else {                         /* die Kappe: gebürstetes Aluminium mit Lichtband */
                float br = fm_cosf((an + 45.0f) * 0.0349066f), g = 150.0f + 62.0f * br * br * (br > 0 ? 1.0f : 0.55f)
                                                                     - 28.0f * (d / rc) + 18.0f * lam;
                if (d > rc - 1.1f)
                    g = 92.0f + 40.0f * lam;  /* der Rand der Kappe */
                c = rgb3(g, g, g + 4.0f);
            }
            /* der Zeiger: weiß auf dem Rock, eine Kerbe in der Kappe */
            proj = dx * ux + dy * uy;
            perp = fabsf_(dx * uy - dy * ux);
            if (proj > r * 0.18f && proj < r - 1.2f) {
                float pc = sat01(1.25f - perp);
                if (pc > 0.0f)
                    c = mix3(c, d > rc ? rgb3(250, 250, 250) : rgb3(40, 40, 44), pc * (d > rc ? 1.0f : 0.8f));
            }
            px_blend(x, y, c, cov);
        }
}

/* ---- ein Wertschild unter dem Regler ---- */
static void value_pill(int32_t cx, int32_t y, const char *t, int hot)
{
    int32_t w = text_w(&FONT_XS, t) + 12;
    if (w < 40)
        w = 40;
    if (hot)
        rrect(cx - w / 2 - 1, y - 1, w + 2, 17, 8.5f, c3(C_YEL), c3(C_YEL_DIM), 0.85f);
    rrect(cx - w / 2, y, w, 15, 7.5f, rgb3(10, 10, 12), rgb3(18, 18, 20), 1.0f);
    ctext(cx, y + 2, &FONT_XS, t, hot ? C_YEL : C_WHITE);
}

static void draw_knob(int k, int32_t cx, int32_t y, uint32_t now)
{
    kdef d = kdef_of(k);
    int v = knob_get(d), hot = ui.knob_at[k] && now - ui.knob_at[k] < 700u;
    char t[20];
    if (v < 0 || d.kind == K_NONE)
        return;
    if (hot)
        text_glow(cx - text_w(&FONT_XS, knob_label(d)) / 2, y, &FONT_XS, knob_label(d), C_YEL, C_YEL_DIM);
    else
        ctext(cx, y, &FONT_XS, knob_label(d), C_WHITE);
    knob_pro((float)cx, (float)(y + 38), 15.0f, (float)v / 127.0f, hot, knob_bip(d));
    val_text(d, t);
    value_pill(cx, y + 66, t, hot);
}

/* ---- die Seitengruppen: kleine Leuchtpunkte, wo man in der Gruppe ist ---- */
static void group_of(int pg, int *i, int *n)
{
    static const uint8_t G0[NPAGES] = {0, 0, 0, 1, 0, 1, 2, 3, 4, 5, 0, 1, 0, 1, 2, 3, 0, 1, 0, 1};
    static const uint8_t GN[NPAGES] = {1, 1, 2, 2, 6, 6, 6, 6, 6, 6, 2, 2, 4, 4, 4, 4, 2, 2, 2, 2};
    *i = G0[pg];
    *n = GN[pg];
}

/* ---- das OLED ---- */
#define OX0 7
#define OY0 5
#define OX1 233
#define OY1 95

static void oled_bezel(void)
{
    rrect(2, 0, 236, 100, 8.0f, rgb3(255, 226, 70), rgb3(196, 150, 6), 1.0f);   /* der gelbe Rahmen */
    rrect(5, 3, 230, 94, 5.0f, rgb3(10, 10, 12), rgb3(0, 0, 0), 1.0f);         /* das schwarze Glas */
}

static void oled_glass(void)             /* ein Hauch Spiegelung oben auf dem Glas */
{
    int32_t x, y;
    for (y = OY0; y < OY0 + 22; y++) {
        float a = 0.045f * (1.0f - (float)(y - OY0) / 22.0f);
        for (x = OX0 + 2; x < OX1 - 2; x++)
            px_glow(x, y, rgb3(255, 255, 255), a * (0.6f + 0.4f * (float)(OX1 - x) / 226.0f) * 0.25f);
    }
}

static void oled_grid(void)              /* ein feines Raster wie auf einem Oszilloskop */
{
    int32_t x, y;
    for (y = 14; y <= 86; y += 12)
        for (x = 12; x <= 228; x += 4)
            px_blend(x, y, rgb3(60, 90, 90), y == 50 ? 0.55f : 0.28f);
}

static void oled_title(const char *a, const char *b)
{
    int32_t x = cv_text(12, 7, &FONT_XS, a, C_OLED_DIM);
    if (b)
        text_glow(x + 6, 7, &FONT_XS, b, C_WHITE, RGB(20, 60, 60));
}

/* eine Wellenform-Vorschau (ohne Signal: so sieht die WAVE-Stellung aus) */
static float wave_preview(float ph, int wi, float r2)
{
    float p1 = ph - (float)(int)ph, p2 = ph * r2;
    p2 -= (float)(int)p2;
    switch (wi) {
    case 0: return 1.0f - 4.0f * fabsf_(p1 - 0.5f);
    case 1: return 2.0f * p1 - 1.0f;
    case 2: return (2.0f * p1 - 1.0f + 2.0f * p2 - 1.0f) * 0.5f;
    case 3: return (2.0f * p1 - 1.0f + (p2 < 0.5f ? 1.0f : -1.0f)) * 0.5f;
    case 4: return ((p1 < 0.5f ? 1.0f : -1.0f) + (p2 < 0.5f ? 1.0f : -1.0f)) * 0.5f;
    case 5: return p1 < 0.16f ? 1.0f : -0.6f;
    default: {
        float m = 1.0f - 4.0f * fabsf_(p2 - 0.5f), q = p1 + (wi == 6 ? 0.12f : 0.35f) * m;
        q -= (float)(int)q;
        if (q < 0.0f)
            q += 1.0f;
        return 1.0f - 4.0f * fabsf_(q - 0.5f);
    }
    }
}

static void oled_scope(void)             /* PANEL: das echte Ausgangssignal, auf einen Nulldurchgang getriggert */
{
    uint32_t w = scope_w, i, start = w - 240u, best = w - 240u;
    float pk = 0.0f, g, px = 0, py = 0;
    int wi = mn_wave_index(edit.p[P_WAVE]), j;
    col3 teal = c3(C_TEAL);
    for (i = w - 600u; i != w - 240u; i++) {   /* der letzte steigende Nulldurchgang vor dem Fenster */
        float a = scope_ring[(i - 1u) % SCOPE_N], b = scope_ring[i % SCOPE_N];
        if (a <= 0.0f && b > 0.0f)
            best = i;
    }
    start = best;
    for (j = 0; j < 216; j++) {
        float v = fabsf_(scope_ring[(start + (uint32_t)j) % SCOPE_N]);
        if (v > pk)
            pk = v;
    }
    oled_grid();
    if (pk < 0.002f) {                   /* still: die Form der WAVE-Stellung, gedimmt */
        float r2 = fm_exp2f(mn_tune2_semis(edit.p[P_TUNE2]) / 12.0f);
        for (j = 0; j <= 216; j += 2) {
            float y = 50.0f - wave_preview((float)j / 108.0f, wi, r2) * 28.0f, x = 12.0f + (float)j;
            if (j)
                neon(px, py, x, y, teal, 0.45f);
            px = x;
            py = y;
        }
    } else {
        g = 34.0f / (pk < 0.08f ? 0.08f : pk);
        for (j = 0; j < 216; j++) {
            float y = 50.0f - scope_ring[(start + (uint32_t)j) % SCOPE_N] * g, x = 12.0f + (float)j;
            y = y < 10.0f ? 10.0f : y > 90.0f ? 90.0f : y;
            fill_col((int32_t)x, y, 50.0f, teal, 0.10f);
            if (j)
                neon(px, py, x, y, teal, 1.0f);
            px = x;
            py = y;
        }
    }
    oled_title("WAVE", MN_WAVE_NAME[wi]);
}

static void oled_filter(void)            /* die Filterkurve, wie sie gerade wirklich steht (Hüllkurve, Modulatoren, Tasten):
                                          * sie atmet mit; wo der CUTOFF-Regler steht, zeigt eine blasse Linie */
{
    float knob = (float)edit.p[P_CUTOFF] / 127.0f, q = (float)edit.p[P_RESON] / 127.0f;
    float hp = edit.p[P_HPCUT] ? (float)edit.p[P_HPCUT] / 127.0f : 0.0f, px = 0, py = 0, fc, peak_y = 90.0f, peak_x = 14.0f;
    col3 teal = c3(C_TEAL), yel = c3(C_YEL);
    int32_t x, y;
    fc = eng.cut_hz > 0.0f && eng.ae.stage ? sat01(fm_log2f(eng.cut_hz / 20.0f) / 10.0f) : knob;
    oled_grid();
    for (y = 12; y < 90; y += 3)         /* der Regler: gestrichelt */
        px_blend(14 + (int32_t)(knob * 212.0f), y, yel, 0.35f);
    for (x = 0; x <= 212; x++) {
        float f = (float)x / 212.0f, d = (f - fc) * 10.0f, db, yy;
        db = d < 0.0f ? q * 18.0f * fm_exp2f(d * 3.0f) : q * 18.0f * fm_exp2f(-d * 6.0f) - d * 24.0f;
        if (hp > 0.0f && f < hp)
            db -= (hp - f) * 120.0f;
        yy = 44.0f - db * 1.1f;
        yy = yy < 10.0f ? 10.0f : yy > 90.0f ? 90.0f : yy;
        if (yy < peak_y) {
            peak_y = yy;
            peak_x = 14.0f + (float)x;
        }
        fill_col(14 + x, yy, 90.0f, teal, 0.13f);
        if (x)
            neon(px, py, 14.0f + (float)x, yy, teal, 1.0f);
        px = 14.0f + (float)x;
        py = yy;
    }
    if (q > 0.08f) {                     /* die Resonanzspitze glüht */
        bloom(peak_x, peak_y, 6.0f + q * 6.0f, yel, 0.5f + 0.4f * q);
        aa_disc(peak_x, peak_y, 2.0f, rgb3(255, 255, 230), 1.0f);
    }
    {
        char t[16];
        hz_text(eng.cut_hz > 0.0f && eng.ae.stage ? eng.cut_hz : mn_cutoff_hz(edit.p[P_CUTOFF]), t);
        oled_title("VCF 24DB LP", t);
    }
}

/* eine ADSR-Kurve; dazu wo die Hüllkurve gerade steht */
static void oled_env(int filt)
{
    const uint8_t *p = edit.p;
    int a = filt ? p[P_FA] : p[P_AA], dd = filt ? p[P_FD] : p[P_AD], s = filt ? p[P_FS] : p[P_AS];
    int r = filt ? p[P_FR] : p[P_AR], tm = filt ? p[P_FTIME] : p[P_ATIME];
    const mn_adsr *env = filt ? &eng.fe : &eng.ae;
    float sc = tm < 64 ? fm_exp2f(-((float)tm - 64.0f) / 63.0f * 3.3219f) : fm_exp2f(-((float)tm - 64.0f) / 63.0f * 2.585f);
    float wa = fm_sqrtf(mn_env_ms(a, 1) * sc), wd = fm_sqrtf(mn_env_ms(dd, 0) * sc), ws = fm_sqrtf(300.0f);
    float wr = fm_sqrtf(mn_env_ms(r, 0) * sc), tot = wa + wd + ws + wr, sl = (float)s / 127.0f;
    float X0 = 14.0f, W = 212.0f, Y0 = 16.0f, H = 70.0f, xa, xd, xs, xr, px = X0, py = Y0 + H;
    col3 teal = c3(C_TEAL), yel = c3(C_YEL);
    int i;
    xa = X0 + wa / tot * W;
    xd = xa + wd / tot * W;
    xs = xd + ws / tot * W;
    xr = X0 + W;
    oled_grid();
    for (i = 1; i <= 64; i++) {          /* die Kurve, Stück für Stück, mit Fläche */
        float t = (float)i / 64.0f, x, lv;
        x = X0 + t * W;
        if (x <= xa)
            lv = (x - X0) / (xa - X0 + 0.01f);
        else if (x <= xd)
            lv = sl + (1.0f - sl) * fm_exp2f(-(x - xa) / (xd - xa + 0.01f) * 6.0f);
        else if (x <= xs)
            lv = sl + (1.0f - sl) * 0.0156f;
        else
            lv = sl * fm_exp2f(-(x - xs) / (xr - xs + 0.01f) * 6.0f);
        {
            float y = Y0 + H - lv * H;
            int32_t c;
            for (c = (int32_t)px; c < (int32_t)x; c++)
                fill_col(c, py + (y - py) * ((float)c - px) / (x - px + 0.01f), Y0 + H, teal, 0.12f);
            neon(px, py, x, y, teal, 1.0f);
            px = x;
            py = y;
        }
    }
    if (env->stage) {                    /* wo sie gerade steht: ein leuchtender Punkt */
        float lv = env->level, x;
        if (env->stage == 1)
            x = X0 + (xa - X0) * lv;
        else if (env->stage == 2)
            x = sl < 0.999f ? xa + (xd - xa) * sat01((1.0f - lv) / (1.0f - sl + 1e-4f)) : xd;
        else
            x = xs + (xr - xs) * sat01(1.0f - lv / (sl > 0.02f ? sl : 0.02f));
        bloom(x, Y0 + H - lv * H, 9.0f, yel, 0.9f);
        aa_disc(x, Y0 + H - lv * H, 2.2f, rgb3(255, 255, 230), 1.0f);
    }
    oled_title(filt ? "FILTER ADSR" : "AMP ADSR", 0);
}

static void oled_mod(int m, int dest_page)
{
    int mode = mode_of(m), i;
    col3 teal = c3(C_TEAL), yel = c3(C_YEL);
    char t[24] = "M";
    t[1] = (char)('1' + m);
    t[2] = 0;
    if (dest_page) {                     /* die Ziele: Balken nach links und rechts, das gewählte gelb */
        for (i = 0; i < MN_NDEST; i++) {
            int v = edit.lvl[m][i], sel = i == ui.dest[m];
            int32_t x = 12 + (i % 2) * 112, y = 20 + (i / 2) * 10, w = (v - 64) * 32 / 63;
            col3 c = sel ? yel : teal;
            cv_text(x, y, &FONT_XS, MN_DEST_NAME[i], sel ? C_YEL : v != 64 ? C_WHITE : C_OLED_DIM);
            px_blend(x + 66, y + 5, rgb3(90, 90, 96), 1.0f);
            aa_line((float)(x + 34), (float)y + 5.5f, (float)(x + 98), (float)y + 5.5f, rgb3(50, 54, 58), 1.0f);
            if (w) {
                int32_t a0 = w > 0 ? x + 66 : x + 66 + w;
                rrect(a0, y + 3, w > 0 ? w : -w, 5, 2.5f, c, mix3(c, rgb3(0, 0, 0), 0.3f), 1.0f);
                bloom((float)(w > 0 ? a0 + w : a0), (float)y + 5.5f, 5.0f, c, sel ? 0.8f : 0.4f);
            }
        }
        text_cat(t, " DEST");
        oled_title(t, MN_MODE_NAME[mode]);
        return;
    }
    if (mode == MODE_STEP) {             /* die Step-Werte als Säulen; die laufende leuchtet */
        int cnt = scount(m), cur = mn_mod_step(&eng, m) % cnt;
        for (i = 0; i < cnt; i++) {
            int32_t w = 212 / cnt, x = 14 + i * w, h = edit.mstep[m][i] * 62 / 127;
            col3 c = i == ui.msel[m] ? yel : teal;
            float al = i == cur ? 1.0f : 0.55f;
            if (h > 0)
                rrect(x + 1, 88 - h, w - 3, h + 1, 2.0f, mix3(c, rgb3(255, 255, 255), i == cur ? 0.35f : 0.0f),
                      mix3(c, rgb3(0, 0, 0), 0.55f), al);
            if (i == cur)
                bloom((float)x + (float)w * 0.5f, (float)(88 - h), 10.0f, c, 0.8f);
        }
        oled_title(t, MN_MODE_NAME[mode]);
        return;
    }
    {   /* LFO, Hüllkurve, Zufall: der Ausgang über die Zeit, mit Fläche zur Nulllinie */
        int32_t x;
        float px = 0, py = 0;
        oled_grid();
        for (x = 0; x < 96; x++) {
            float v = ui.scope[m][(ui.scope_pos + (uint32_t)x) % 96u];
            float nx = 14.0f + (float)x * 2.22f, ny = 50.0f - v * 32.0f;
            int32_t c;
            if (x) {
                for (c = (int32_t)px; c < (int32_t)nx; c++)
                    fill_col(c, py + (ny - py) * ((float)c - px) / (nx - px), 50.0f, teal, 0.16f);
                neon(px, py, nx, ny, teal, 1.0f);
            }
            px = nx;
            py = ny;
        }
        bloom(px, py, 9.0f, yel, 0.9f);
        aa_disc(px, py, 2.2f, rgb3(255, 255, 230), 1.0f);
        oled_title(t, MN_MODE_NAME[mode]);
    }
}

static void oled_fx(void)                /* die Kette FX1 -> FX2 -> FX3: die Blöcke leuchten mit ihrer Mischung */
{
    static const char *const T[3] = {"FX1", "FX2", "FX3"};
    col3 teal = c3(C_TEAL), yel = c3(C_YEL);
    int i;
    for (i = 0; i < 3; i++) {
        int32_t x = 12 + i * 76;
        int on = ui.page == PG_FX1 + i || ui.page == PG_FXP;
        int mix = edit.p[i == 0 ? P_X1MIX : i == 1 ? P_X2MIX : P_X3MIX];
        float lit = (float)mix / 127.0f;
        const char *nm = i == 0 ? MN_X1_NAME[mn_list_index(P_X1TYPE, edit.p[P_X1TYPE])]
                         : i == 1 ? MN_X2_NAME[mn_list_index(P_X2TYPE, edit.p[P_X2TYPE])]
                                  : MN_X3_NAME[mn_list_index(P_X3TYPE, edit.p[P_X3TYPE])];
        if (on)
            rrect(x - 1, 17, 68, 52, 7.0f, yel, mix3(yel, rgb3(0, 0, 0), 0.5f), 0.9f);
        rrect(x, 18, 66, 50, 6.0f, mix3(rgb3(14, 22, 24), teal, lit * 0.25f), rgb3(6, 8, 10), 1.0f);
        if (lit > 0.01f)
            bloom((float)x + 33.0f, 43.0f, 26.0f, teal, lit * 0.35f);
        if (on)
            text_glow(x + 33 - text_w(&FONT_XS, T[i]) / 2, 23, &FONT_XS, T[i], C_YEL, C_YEL_DIM);
        else
            ctext(x + 33, 23, &FONT_XS, T[i], C_OLED_DIM);
        ctext(x + 33, 42, &FONT_XS, nm, mix ? C_WHITE : C_OLED_DIM);
        rrect(x + 4, 76, 58, 5, 2.5f, rgb3(30, 34, 36), rgb3(30, 34, 36), 1.0f);
        if (mix)
            rrect(x + 4, 76, 4 + mix * 54 / 127, 5, 2.5f, mix3(teal, rgb3(255, 255, 255), 0.3f), teal, 1.0f);
        if (i < 2) {
            neon((float)(x + 67), 43.0f, (float)(x + 75), 43.0f, teal, 0.9f);
        }
    }
    cv_text(12, 84, &FONT_XS, "MIX", C_OLED_DIM);
}

static void oled_seq(void)               /* 32 Steps: die Note als Höhe, Gate als Breite, TIE verbunden; der laufende leuchtet */
{
    int i, len = mn_seq_len(&eng), cur = mn_step_now(&eng);
    col3 teal = c3(C_TEAL), yel = c3(C_YEL);
    char t[28] = "", n[4];
    for (i = 0; i < MN_STEPS; i++) {
        const mn_step *s = &edit.seq[i];
        int32_t x = 12 + (i % 16) * 13, y = 8 + (i / 16) * 34;
        int in = i < len;
        float al = in ? 1.0f : 0.25f;
        if (i == ui.ssel)
            rrect(x - 2, y - 2, 15, 32, 3.0f, yel, mix3(yel, rgb3(0, 0, 0), 0.5f), 0.9f);
        rrect(x - 1, y - 1, 13, 30, 2.5f, rgb3(12, 16, 18), rgb3(4, 6, 8), 1.0f);
        if (s->gate == MN_REST) {
            aa_line((float)x + 3.0f, (float)y + 24.5f, (float)x + 8.0f, (float)y + 24.5f, rgb3(90, 96, 100), al);
        } else {
            float h = 3.0f + (float)(s->note - 24) * 21.0f / 72.0f;
            int32_t w = s->gate == MN_TIE ? 13 : 3 + s->gate * 8 / 127;
            col3 c = i == cur ? mix3(teal, rgb3(255, 255, 255), 0.5f) : teal;
            if (w > 11 && i % 16 == 15)
                w = 11;
            rrect(x, y + 26 - (int32_t)h, w, 3, 1.5f, c, c, al * (0.45f + 0.55f * (float)s->vel / 127.0f));
            if (i == cur)
                bloom((float)x + 5.0f, (float)y + 27.0f - h, 9.0f, teal, 0.9f);
        }
    }
    note_text(edit.seq[ui.ssel].note, t);
    cv_text(12, 80, &FONT_XS, "STEP", C_OLED_DIM);
    int_text(ui.ssel + 1, n);
    cv_text(44, 80, &FONT_XS, n, C_WHITE);
    text_glow(66, 80, &FONT_XS, edit.seq[ui.ssel].gate == MN_REST ? "REST" : t, C_WHITE, RGB(20, 60, 60));
    if (ui.rec) {
        const char *r = ui.ssel + 1 >= len ? "APPEND" : "RECORD";
        int32_t x = 222 - text_w(&FONT_XS, r);
        if ((plat_ms() / 400u) & 1u)
            bloom((float)x - 8.0f, 85.0f, 7.0f, c3(C_RED), 0.9f);
        aa_disc((float)x - 8.0f, 85.0f, 3.0f, c3(C_RED), 1.0f);
        text_glow(x, 80, &FONT_XS, r, C_RED, RGB(70, 12, 8));
    } else if (ui.copied >= 0) {
        text_glow(228 - text_w(&FONT_XS, "COPIED"), 80, &FONT_XS, "COPIED", C_YEL, C_YEL_DIM);
    }
}

static void oled_glo(void)
{
    char t[32] = "";
    col3 teal = c3(C_TEAL);
    int_text((int32_t)(mn_bpm(&eng) + 0.5f), t);
    text_glow(14, 10, &APP_BIG_FONT, t, C_WHITE, RGB(20, 70, 70));
    cv_text(16 + text_w(&APP_BIG_FONT, t), 28, &FONT_XS, eng.ext ? "BPM  MIDI CLOCK" : "BPM", C_TEAL);
    cv_text(130, 12, &FONT_XS, "KEYS", C_OLED_DIM);
    {
        char nn[12] = "";
        note_text(key_base(), nn);
        text_cat(nn, "-");
        note_text(key_base() + NKEYS - 1, nn + txt_len(nn));
        cv_text(166, 12, &FONT_XS, nn, C_WHITE);
    }
    cv_text(130, 28, &FONT_XS, "HOLD", C_OLED_DIM);
    cv_text(166, 28, &FONT_XS, ui.hold ? "ON" : "OFF", ui.hold ? C_YEL : C_WHITE);
    {   /* die Rechenlast als leuchtender Balken */
        uint32_t cpu = plat_cpu_pct();
        char c[24] = "CPU ";
        int_text((int32_t)cpu, c + 4);
        text_cat(c, "%");
        if (eng.eco)
            text_cat(c, "  ECO");
        cv_text(14, 56, &FONT_XS, c, cpu > 80u ? C_RED : C_WHITE);
        rrect(14, 70, 212, 6, 3.0f, rgb3(30, 34, 36), rgb3(30, 34, 36), 1.0f);
        rrect(14, 70, 6 + (int32_t)(cpu > 100u ? 100u : cpu) * 206 / 100, 6, 3.0f,
              cpu > 80u ? c3(C_RED) : mix3(teal, rgb3(255, 255, 255), 0.3f), cpu > 80u ? c3(C_RED) : teal, 1.0f);
    }
    cv_text(14, 82, &FONT_XS, "MONSUN V" APP_VERSION, C_OLED_DIM);
}

static void oled_save(void)
{
    char t[24] = "TO ";
    const mn_patch *p = slot_patch(ui.save_slot);
    t[3] = (char)('0' + (ui.save_slot + 1) / 10);
    t[4] = (char)('0' + (ui.save_slot + 1) % 10);
    t[5] = 0;
    bloom(120.0f, 40.0f, 60.0f, c3(C_YEL), 0.18f);
    text_glow(120 - text_w(&FONT_B, "SAVE") / 2, 12, &FONT_B, "SAVE", C_YEL, C_YEL_DIM);
    text_glow(120 - text_w(&APP_BIG_FONT, t) / 2, 32, &APP_BIG_FONT, t, C_WHITE, RGB(40, 40, 40));
    ctext(120, 66, &FONT_XS, p->name, C_OLED_DIM);
    ctext(120, 80, &FONT_XS, "PRESET: SLOT   SAVE: OK", C_OLED_DIM);
}

static int oled_animated(void)           /* diese Seiten bewegen sich: das OLED jeden zweiten Frame neu */
{
    return ui.page == PG_MAIN || ui.page == PG_FILTER || ui.page == PG_FENV || ui.page == PG_AENV
           || ui.page == PG_SET1 || ui.page == PG_SET2 || mod_of_page(ui.page) >= 0
           || ((ui.page == PG_SEQ1 || ui.page == PG_SEQ2) && ui.rec) || ui.page == PG_GLO1 || ui.page == PG_GLO2;
}

static void draw_oled(void)
{
    int m = mod_of_page(ui.page);
    oled_bezel();
    if (ui.saving)
        oled_save();
    else if (m >= 0)
        oled_mod(m, (ui.page - PG_M1) & 1);
    else
        switch (ui.page) {
        case PG_MAIN: oled_scope(); break;
        case PG_FILTER: oled_filter(); break;
        case PG_FENV: oled_env(1); break;
        case PG_AENV:
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
    oled_glass();
}

/* ---- der Kopf: der Name leuchtet, der Platz, ein Pegel, das Tempo, der Transport ---- */
static void draw_head(uint32_t now)
{
    col3 teal = c3(C_TEAL), yel = c3(C_YEL);
    char t[24];
    int32_t x;
    cv_begin(240, 30, C_PANEL);
    panel_bg(0, 30, 0);
    if (now < ui.msg_until) {
        x = cv_text(8, 7, &FONT_B, ui.msg[0], C_WHITE);
        text_glow(x + 8, 7, &FONT_B, ui.msg[1], C_YEL, C_YEL_DIM);
    } else {
        bloom(36.0f, 14.0f, 34.0f, yel, 0.10f);
        text_glow(8, 6, &FONT_B, "MONSUN", C_YEL, C_YEL_DIM);
        x = 8 + text_w(&FONT_B, "MONSUN");
        cv_text(x + 6, 4, &FONT_XS, "MONO", C_WHITE);
        cv_text(x + 6, 14, &FONT_XS, "SYNTH", C_SUB);
        /* der Platz als Schild */
        t[0] = (char)('0' + (set.slot + 1) / 10);
        t[1] = (char)('0' + (set.slot + 1) % 10);
        t[2] = set.edited ? '*' : 0;
        t[3] = 0;
        rrect(124, 6, 30, 18, 6.0f, set.edited ? mix3(yel, rgb3(0, 0, 0), 0.55f) : rgb3(56, 58, 62), rgb3(14, 14, 16), 1.0f);
        rrect(125, 7, 28, 16, 5.0f, rgb3(16, 16, 18), rgb3(8, 8, 10), 1.0f);
        ctext(139, 9, &FONT_XS, t, set.edited ? C_YEL : C_WHITE);
        /* der Pegel: acht Segmente, türkis, gelb, rot */
        {
            float pk = out_peak, lv = fm_sqrtf(pk < 1.0f ? pk : 1.0f) * 8.0f;
            int i;
            for (i = 0; i < 8; i++) {
                col3 c = i < 5 ? teal : i < 7 ? yel : c3(C_RED);
                int on = (float)i < lv;
                rrect(160 + i * 4, 9, 3, 12, 1.0f, on ? c : rgb3(40, 42, 44), on ? mix3(c, rgb3(0, 0, 0), 0.3f) : rgb3(30, 32, 34), 1.0f);
                if (on)
                    px_glow(161 + i * 4, 8, c, 0.4f);
            }
        }
        t[0] = 0;
        int_text((int32_t)(mn_bpm(&eng) + 0.5f), t);
        cv_text(212 - text_w(&FONT_XS, t), 9, &FONT_XS, t, eng.ext ? C_TEAL : C_WHITE);
        if (ui.playing) {                /* Play: ein leuchtendes Dreieck */
            int32_t i;
            bloom(222.0f, 15.0f, 9.0f, teal, 0.7f);
            for (i = 0; i < 9; i++) {
                int32_t hh = 12 - i * 4 / 3;
                cv_rect(217 + i, 15 - hh / 2, 1, hh, C_TEAL);
            }
        } else {
            rrect(217, 10, 10, 10, 2.0f, rgb3(150, 152, 158), rgb3(90, 92, 96), 1.0f);
        }
    }
    /* die Kante: eine gelbe Linie, die zu den Seiten verlischt */
    for (x = 4; x < 236; x++) {
        float d = ((float)x - 120.0f) / 116.0f;
        px_blend(x, 29, yel, 0.55f * (1.0f - d * d));
    }
    cv_blit(0, 0);
}

/* ---- die Mitte: die Seite und ihre vier Regler ---- */
static void draw_mid(uint32_t now)
{
    int gi, gn, i;
    cv_begin(240, 110, C_PANEL);
    panel_bg(0, 110, 30);
    {
        int32_t x = 8 + text_w(&FONT_XS, PAGE_NAME[ui.page]);
        text_glow(8, 3, &FONT_XS, PAGE_NAME[ui.page], C_YEL, C_YEL_DIM);
        cv_text(x + 8, 3, &FONT_XS, edit.name, C_SUB);
    }
    group_of(ui.page, &gi, &gn);
    for (i = 0; i < gn && gn > 1; i++) {     /* wo in der Gruppe: Leuchtpunkte */
        float x = 232.0f - (float)(gn - 1 - i) * 9.0f;
        if (i == gi) {
            bloom(x, 7.5f, 5.0f, c3(C_YEL), 0.8f);
            aa_disc(x, 7.5f, 2.4f, c3(C_YEL), 1.0f);
        } else {
            aa_disc(x, 7.5f, 2.0f, rgb3(70, 72, 76), 1.0f);
        }
    }
    if (now < ui.big_until) {
        bloom(120.0f, 56.0f, 70.0f, c3(C_YEL), 0.12f);
        ctext(120, 26, &FONT_XS, ui.big[1], C_SUB);
        text_glow(120 - text_w(&APP_BIG_FONT, ui.big[0]) / 2, 44, &APP_BIG_FONT, ui.big[0], C_YEL, C_YEL_DIM);
    } else {
        for (i = 0; i < 4; i++)
            draw_knob(i, 33 + i * 58, 16, now);
    }
    cv_blit(0, 30);
}

static void draw(uint32_t mask)
{
    uint32_t now = plat_ms();
    if (mask & R_HEAD)
        draw_head(now);
    if (mask & R_MID)
        draw_mid(now);
    if (mask & R_OLED) {
        cv_begin(240, 100, C_PANEL);
        panel_bg(0, 100, 140);
        draw_oled();
        cv_blit(0, 140);
    }
}

/* ---- die Eröffnung: Regen fällt, ein Monsun-Wirbel dreht sich auf, der Name glüht auf ---- */
#define INTRO_MS 2400u
static void intro_scene(float t, int32_t y0, int32_t h)
{
    col3 teal = c3(C_TEAL), yel = c3(C_YEL);
    float grow = sat01(t / 0.9f), ease = 1.0f - (1.0f - grow) * (1.0f - grow) * (1.0f - grow);
    float logo = sat01((t - 0.8f) / 0.6f), sub = sat01((t - 1.35f) / 0.5f);
    int i;
    cv_oy = -y0;                          /* in Bildschirmkoordinaten zeichnen: dieser Streifen ist y0 .. y0+h */
    panel_bg(y0, h, y0);
    for (i = 0; i < 46; i++) {           /* der Regen: schräge Striche, die verschieden schnell fallen */
        uint32_t s = (uint32_t)i * 2654435761u;
        float x = (float)(s % 260u) - 10.0f, sp = 160.0f + (float)((s >> 8) % 140u), len = 8.0f + (float)((s >> 16) % 14u);
        float y = fmod_pos(t * sp + (float)((s >> 4) % 300u), 300.0f) - 30.0f, a = 0.10f + 0.22f * (float)((s >> 20) % 10u) / 10.0f;
        if (y + len < (float)y0 || y > (float)(y0 + h))
            continue;
        aa_line(x, y, x - len * 0.25f, y + len, mix3(teal, rgb3(255, 255, 255), 0.4f), a * (1.0f - 0.6f * logo));
    }
    for (i = 0; i < 5; i++) {            /* der Wirbel */
        float r = (14.0f + (float)i * 11.0f) * ease, rot = t * (110.0f + 45.0f * (float)i) * (i & 1 ? -1.0f : 1.0f) + (float)i * 70.0f;
        if (r < 2.0f || 96.0f + r < (float)y0 - 8.0f || 96.0f - r > (float)(y0 + h) + 8.0f)
            continue;
        ring_arc(120.0f, 96.0f, r, i == 0 ? 3.5f : 3.0f, rot, rot + 200.0f + (float)i * 15.0f,
                 i & 1 ? mix3(teal, rgb3(0, 0, 0), 0.45f) : teal, ease, 5.0f);
    }
    if (96 + 30 > y0 && 96 - 30 < y0 + h) {
        bloom(120.0f, 96.0f, 26.0f + 6.0f * fm_sinf(t * 9.0f), yel, 0.55f * ease);
        aa_disc(120.0f, 96.0f, 5.5f * ease, yel, 1.0f);
    }
    if (logo > 0.0f) {                   /* der Name glüht auf */
        col3 c = mix3(c3(C_PANEL), yel, logo);
        uint16_t cc = c565(c), halo = c565(mix3(c3(C_PANEL), c3(C_YEL_DIM), logo));
        bloom(120.0f, 176.0f, 80.0f, yel, 0.22f * logo * (1.4f - 0.4f * logo));
        text_glow(120 - text_w(&APP_BIG_FONT, "MONSUN") / 2, 160, &APP_BIG_FONT, "MONSUN", cc, halo);
    }
    if (sub > 0.0f) {
        ctext(120, 198, &FONT_XS, "MONO SYNTHESIZER", c565(mix3(c3(C_PANEL), c3(C_WHITE), sub)));
        {
            int32_t x;
            for (x = 60; x < 180; x++) {
                float d = ((float)x - 120.0f) / 60.0f;
                px_blend(x, 213, yel, sub * 0.7f * (1.0f - d * d));
            }
        }
        ctext(120, 220, &FONT_XS, "by swtdr34mz", c565(mix3(c3(C_PANEL), c3(C_SUB), sub)));
    }
    cv_oy = 0;
}

static void intro_draw(float t)
{
    cv_begin(240, 110, C_PANEL);
    intro_scene(t, 0, 110);
    cv_blit(0, 0);
    cv_begin(240, 110, C_PANEL);
    intro_scene(t, 110, 110);
    cv_blit(0, 110);
    cv_begin(240, 20, C_PANEL);
    intro_scene(t, 220, 20);
    cv_blit(0, 220);
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
    {
        uint32_t w = scope_w;
        float pk = out_peak * 0.92f;     /* der Pegel fürs Display: Spitze, langsam fallend */
        for (i = 0; i < n; i++) {
            float m = 0.5f * (bl[i] + br[i]), a = m < 0.0f ? -m : m;
            lr[2 * i] = (int32_t)(bl[i] * gain);
            lr[2 * i + 1] = (int32_t)(br[i] * gain);
            scope_ring[(w + i) % SCOPE_N] = m;
            if (a > pk)
                pk = a;
        }
        scope_w = w + n;
        out_peak = pk;
    }
}

void app_panic(void)
{
    ui.playing = 0;
    mn_play(&eng, 0);
    mn_all_off(&eng);
}

void app_splash(void)                    /* der Startbildschirm: das Ende der Eröffnung, still */
{
    lcd_fill(0, 0, 240, 240, C_PANEL);
    intro_draw(3.0f);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_PANEL);
    touch_on(1);
    ui.step_drawn = -2;
    ui.intro = 1;
    ui.intro_at = plat_ms();
    ui.redraw = R_ALL;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    int32_t s, lv, m, pk;
    if (ui.intro) {                      /* die Eröffnung: bis sie durch ist, oder eine Taste / ein Knopf drückt */
        uint32_t t = now - ui.intro_at;
        if (t < INTRO_MS && !plat_keys() && !plat_buttons()) {
            intro_draw((float)t * 0.001f);
            plat_lights(0, 0, 0, 0, 0);
            return;
        }
        ui.intro = 0;
        ui.keys = plat_keys();           /* die Taste, die abgebrochen hat, spielt nicht */
        ui.btn = plat_buttons();
        ui.redraw = R_ALL;
    }
    input();
    lights();
    ui.frame++;
    if (ann_on() && ann_where()) {
        char t[48] = "MONSUN. ";
        text_cat(t, PAGE_NAME[ui.page]);
        text_cat(t, ". ");
        text_cat(t, edit.name);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    if (ui.playing != (uint8_t)eng.running) {
        ui.playing = (uint8_t)eng.running;
        ui.redraw |= R_HEAD;
    }
    if (ui.saving && now > ui.save_until) {
        ui.saving = 0;
        ui.redraw |= R_OLED;
    }
    for (m = 0; m < 3; m++)              /* das Oszilloskop der Modulatoren */
        ui.scope[m][ui.scope_pos] = mn_mod_out(&eng, m);
    ui.scope_pos = (uint8_t)((ui.scope_pos + 1u) % 96u);
    if (oled_animated() && !(ui.frame & 1u))   /* lebende Kurven: 30 Bilder pro Sekunde */
        ui.redraw |= R_OLED;
    pk = (int32_t)(fm_sqrtf(out_peak < 1.0f ? out_peak : 1.0f) * 8.0f);
    if (pk != ui.peak_drawn) {           /* der Pegel im Kopf */
        ui.peak_drawn = pk;
        ui.redraw |= R_HEAD;
    }
    s = mn_step_now(&eng);
    lv = (int32_t)(eng.level_now * 8.0f);
    if (s != ui.step_drawn || lv != ui.lvl_drawn) {
        ui.step_drawn = s;
        ui.lvl_drawn = lv;
        if (ui.page == PG_SEQ1 || ui.page == PG_SEQ2)
            ui.redraw |= R_OLED;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw |= R_HEAD;
    }
    if (ui.big_until && now >= ui.big_until) {
        ui.big_until = 0;
        ui.redraw |= R_MID;
    }
    {   /* ein gedrehter Regler verlischt nach 0.7 s wieder */
        int k;
        for (k = 0; k < 4; k++)
            if (ui.knob_at[k] && now - ui.knob_at[k] >= 700u) {
                ui.knob_at[k] = 0;
                ui.redraw |= R_MID;
            }
    }
    if (now - ui.load_at >= 500u) {
        uint32_t l = plat_cpu_pct() | plat_xruns() << 8;
        ui.load_at = now;
        if (l != ui.load_shown)
            ui.load_shown = l;
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
        uint32_t r = ui.redraw;
        ui.redraw = 0;
        draw(r);
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
