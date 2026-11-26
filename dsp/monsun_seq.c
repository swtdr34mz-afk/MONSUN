/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – Noten, der 32-Step-Notensequencer, der Takt (intern oder MIDI-Clock) und die Schnittstelle.
 * Teil von monsun_core.c */

void mn_init(mn_engine *e, float sr, mn_patch *pt)
{
    int i;
    char *z = (char *)e;
    for (i = 0; i < (int)sizeof *e; i++)
        z[i] = 0;
    e->sr = sr;
    e->pt = pt;
    e->note = -1;
    e->seq_note = -1;
    e->cur = -1;
    e->rs = 0x6C8E9CF5u;
    e->bend_range = 2.0f;
    e->pitch = e->pitch_to = 48.0f;
    for (i = 0; i < 3; i++)
        e->mod[i].rs = 0x2545F491u * (uint32_t)(i + 1) + 12345u;
    e->dr.rs = 0x1B873593u;
    e->dr.sc_t = 1e9f;
    e->g_step = -1;
    e->sc_now = 1.0f;
    e->dirty = 1;
}

void mn_changed(mn_engine *e) { e->dirty = 1; }

/* ---- die Ereignisse: aus der Hauptschleife in den Ring, im Audio-Teil ausgeführt ---- */
static void ev_push(mn_engine *e, int type, int a, int b)
{
    uint32_t w = e->ev_w;
    if (w - e->ev_r >= MN_NEV)
        return;                          /* voll: verworfen */
    e->ev[w % MN_NEV].type = (uint8_t)type;
    e->ev[w % MN_NEV].a = (uint8_t)a;
    e->ev[w % MN_NEV].b = (uint8_t)b;
    e->ev_w = w + 1;
}
void mn_note_on(mn_engine *e, int note, int vel) { ev_push(e, MEV_ON, note & 127, vel < 1 ? 1 : vel & 127); }
void mn_note_on_play(mn_engine *e, int note, int vel) { ev_push(e, MEV_ONF, note & 127, vel < 1 ? 1 : vel & 127); }
void mn_note_off(mn_engine *e, int note) { ev_push(e, MEV_OFF, note & 127, 0); }
void mn_all_off(mn_engine *e) { ev_push(e, MEV_ALLOFF, 0, 0); }
void mn_play(mn_engine *e, int on) { ev_push(e, on ? MEV_PLAY : MEV_STOP, 0, 0); }
void mn_continue(mn_engine *e) { ev_push(e, MEV_CONT, 0, 0); }
void mn_midi_clock(mn_engine *e) { ev_push(e, MEV_CLOCK, 0, 0); }
void mn_bend(mn_engine *e, int v14) { ev_push(e, MEV_BEND, v14 & 127, (v14 >> 7) & 127); }
void mn_hold(mn_engine *e, int on) { ev_push(e, MEV_HOLD, on != 0, 0); }

/* ---- Takt ---- */
float mn_bpm(const mn_engine *e)
{
    if (e->ext && e->ext_bpm > 0.0f)
        return e->ext_bpm;
    return (float)(e->pt->bpm < 30 ? 30 : e->pt->bpm > 300 ? 300 : e->pt->bpm);
}
int mn_dvs(const mn_engine *e) { return MN_DVS[mn_list_index(P_SDVS, e->pt->p[P_SDVS])]; }
int mn_seq_len(const mn_engine *e) { return mn_list_index(P_SLEN, e->pt->p[P_SLEN]) + 1; }
int mn_step_now(const mn_engine *e) { return e->running ? e->cur : -1; }
static float step_len(const mn_engine *e) { return e->sr * 60.0f / mn_bpm(e) * 4.0f / (float)mn_dvs(e); }

/* ---- die Tasten: ein Stapel, die letzte gedrückte klingt (wie am Typhon) ---- */
static void stack_remove(mn_engine *e, int note)
{
    int i, j = 0;
    for (i = 0; i < e->nheld; i++)
        if (e->held[i] != note)
            e->held[j++] = e->held[i];
    e->nheld = j;
}
static void stack_push(mn_engine *e, int note)
{
    int i;
    stack_remove(e, note);
    if (e->nheld >= 12) {                /* voll: die älteste fällt raus */
        for (i = 1; i < 12; i++)
            e->held[i - 1] = e->held[i];
        e->nheld = 11;
    }
    e->held[e->nheld++] = (uint8_t)note;
}

static void key_on(mn_engine *e, int note, int vel, int force)
{
    if (e->running && !force) {          /* der Sequencer läuft: die Taste transponiert ihn (C4 = unverändert) */
        e->transpose = note - 60;
        return;
    }
    stack_push(e, note);
    voice_on(e, note, (float)vel * (1.0f / 127.0f), 0);
    mods_step(e, 1);                     /* die Step-Modulatoren beginnen mit jeder Note von vorn */
    e->step_clock = step_len(e);
}

static void key_off(mn_engine *e, int note)
{
    int was = e->nheld;
    stack_remove(e, note);
    if (was == e->nheld)
        return;                          /* war keine gespielte (eine Transpose-Taste) */
    if (e->nheld > 0) {
        int top = e->held[e->nheld - 1];
        if (top != e->note)
            voice_on(e, top, e->vel, 0);  /* zurück zur vorigen Taste */
        return;
    }
    if (e->hold || e->seq_note >= 0)
        return;                          /* HOLD: sie klingt weiter */
    voice_off(e);
}

/* ---- der Sequencer ---- */
static void seq_release(mn_engine *e)
{
    if (e->seq_note >= 0 && e->nheld == 0 && !e->hold)
        voice_off(e);
    e->seq_note = -1;
}

static void advance(mn_engine *e)
{
    const mn_step *st;
    int len = mn_seq_len(e), note, play;
    float sl = step_len(e);
    e->cur = (e->cur + 1) % len;
    st = &e->pt->seq[e->cur];
    play = st->gate != MN_REST;
    if (play && e->pt->p[P_SPROB] < 127) {   /* PROB: die Chance, dass der Step spielt */
        e->rs ^= e->rs << 13;
        e->rs ^= e->rs >> 17;
        e->rs ^= e->rs << 5;
        play = (float)(e->rs & 1023u) < (float)e->pt->p[P_SPROB] / 127.0f * 1024.0f;
    }
    if (!play) {
        if (e->tie_hold)                 /* nach einem TIE endet die Note hier */
            seq_release(e);
        e->tie_hold = 0;
        e->gate_left = 0.0f;
        return;
    }
    note = st->note + e->transpose;
    note = note < 21 ? 21 : note > 108 ? 108 : note;
    if (e->tie_hold && e->seq_note >= 0)
        voice_on(e, note, e->vel, 1);    /* gebunden: gleitet ohne neuen Anschlag */
    else
        voice_on(e, note, (float)st->vel * (1.0f / 127.0f), 0);
    e->seq_note = note;
    e->tie_hold = st->gate == MN_TIE;
    e->gate_left = e->tie_hold ? 0.0f : mn_clamp((float)st->gate / 127.0f * sl, 0.004f * e->sr, sl * 1.2f);
}

static void seq_start(mn_engine *e)
{
    e->running = 1;
    e->cur = -1;
    e->to_next = 0.0f;
    e->transpose = 0;
    e->ext_ticks = 0;
    e->tie_hold = 0;
    e->step_clock = step_len(e);
    e->g_step = -1;                      /* der Groove beginnt mit der Sequenz */
    e->g_to_next = 0.0f;
    mods_step(e, 1);
}

static void seq_stop(mn_engine *e)
{
    e->running = 0;
    e->cur = -1;
    e->tie_hold = 0;
    e->transpose = 0;
    seq_release(e);
}

/* 24 Pulse pro Viertel: das Tempo gemessen, und im Takt der Pulse die Steps */
static void clock_tick(mn_engine *e)
{
    if (e->last_clock) {
        uint32_t d = e->counter - e->last_clock;
        if (d > 0u && (float)d < e->sr) {
            float b = e->sr * 60.0f / ((float)d * 24.0f);
            if (b >= 20.0f && b <= 400.0f)
                e->ext_bpm = e->ext_bpm <= 0.0f ? b : e->ext_bpm * 0.9f + b * 0.1f;
        }
    }
    e->last_clock = e->counter ? e->counter : 1u;
    e->ext = 1;
    if (e->running) {
        int tps = 96 / mn_dvs(e);
        if (e->ext_ticks % tps == 0)
            advance(e);
        if (e->gv && e->gv->on && e->ext_ticks % 6 == 0)   /* der Groove: eine Sechzehntel je 6 Pulse */
            groove_step(e, 0);
        e->ext_ticks++;
    }
}

static void events(mn_engine *e)
{
    while (e->ev_r != e->ev_w) {
        const mn_ev *v = &e->ev[e->ev_r % MN_NEV];
        switch (v->type) {
        case MEV_ON: key_on(e, v->a, v->b, 0); break;
        case MEV_ONF: key_on(e, v->a, v->b, 1); break;
        case MEV_OFF: key_off(e, v->a); break;
        case MEV_ALLOFF:
            e->nheld = 0;
            e->seq_note = -1;
            voice_off(e);
            break;
        case MEV_PLAY: seq_start(e); break;
        case MEV_STOP: seq_stop(e); break;
        case MEV_CONT: e->running = 1; break;
        case MEV_CLOCK: clock_tick(e); break;
        case MEV_BEND: e->bend = (float)(((int)v->b << 7 | v->a) - 8192) / 8192.0f * e->bend_range; break;
        case MEV_HOLD:
            e->hold = v->a;
            if (!e->hold && e->nheld == 0 && e->seq_note < 0)
                voice_off(e);
            break;
        default: break;
        }
        e->ev_r++;
    }
}

void mn_render(mn_engine *e, float *l, float *r, int n)
{
    float *out = e->mono;
    int done = 0;
    if (n > 256)
        n = 256;
    events(e);
    if (e->dirty)
        voice_update(e);
    if (e->ext && (float)(e->counter - e->last_clock) > e->sr * 0.5f)
        e->ext = 0;                      /* eine halbe Sekunde ohne Clock: wieder das eigene Tempo */
    while (done < n) {
        int chunk = n - done;
        float base = step_len(e);
        if (e->running && !e->ext) {
            int until;
            if (e->to_next <= 0.0f) {
                float sw = nrm(e->pt->p[P_SSWING]) * 0.5f;   /* SWING: 50 % .. 75 % */
                advance(e);
                e->to_next += base * ((e->cur & 1) ? 1.0f - sw : 1.0f + sw);
            }
            until = (int)e->to_next;
            if ((float)until < e->to_next)
                until++;
            if (until < chunk)
                chunk = until < 1 ? 1 : until;
        }
        if (e->running && !e->ext && e->gv && e->gv->on) {   /* der Groove: seine Sechzehntel, mit seinem Swing */
            int until;
            if (e->g_to_next <= 0.0f) {
                float g16 = e->sr * 15.0f / mn_bpm(e), sw = STYLES[grv_style(e)].swing;
                groove_step(e, done);
                e->g_to_next += g16 * ((e->g_step & 1) ? 1.0f - sw : 1.0f + sw);
            }
            until = (int)e->g_to_next;
            if ((float)until < e->g_to_next)
                until++;
            if (until < chunk)
                chunk = until < 1 ? 1 : until;
        }
        if (e->gate_left > 0.0f) {       /* das Gate des Steps endet mitten im Block */
            int g = (int)e->gate_left;
            if ((float)g < e->gate_left)
                g++;
            if (g < chunk)
                chunk = g < 1 ? 1 : g;
        }
        voice_render(e, out + done, chunk);
        if (e->running && !e->ext)
            e->to_next -= (float)chunk;
        if (e->running && !e->ext && e->gv && e->gv->on)
            e->g_to_next -= (float)chunk;
        if (e->gate_left > 0.0f) {
            e->gate_left -= (float)chunk;
            if (e->gate_left <= 0.0f) {
                e->gate_left = 0.0f;
                if (!e->tie_hold)
                    seq_release(e);
            }
        }
        /* die Step-Modulatoren laufen immer im Takt der Teilung */
        e->step_clock -= (float)chunk;
        if (e->step_clock <= 0.0f) {
            mods_step(e, 0);
            e->step_clock += base;
            if (e->step_clock <= 0.0f)
                e->step_clock = base;
        }
        done += chunk;
    }
    fx_render(e, out, l, r, n);
    if (e->gv && (e->gv->on || e->gqn)) /* GROOVE: die Pumpe und die Drums */
        groove_render(e, l, r, n);
    else
        e->sc_now = 1.0f;
    e->counter += (uint32_t)n;
}
