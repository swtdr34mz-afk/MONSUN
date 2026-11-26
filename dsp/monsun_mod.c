/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – die drei Modulatoren M1..M3, wie am Typhon: je eine Quelle (LFO, Hüllkurve, Zufall, Step-Sequencer)
 * und ein eigener LVL zu jedem Ziel. Sie rechnen alle 8 Samples. Teil von monsun_core.c */

static float rnd_bip(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return (float)(int32_t)*s * (1.0f / 2147483648.0f);
}

/* die Rate eines LFO / Zufalls: frei in Hz, oder (BPM) eine Teilung des Tempos */
static float mod_hz(const mn_engine *e, int sync, int v)
{
    if (sync == SYNC_BPM || sync == SYNC_BPMKEY) {
        int i = (v * (MN_NDIV - 1) + 63) / 127;
        return mn_bpm(e) / 60.0f / MN_DIV_BEATS[i];
    }
    return mn_lfo_hz(v);
}

/* eine neue Note (Tastatur, MIDI, Sequencer-Anschlag): KEY-Sync, Fade, Hüllkurven, Step-Neustart */
static void mods_key(mn_engine *e)
{
    int m;
    for (m = 0; m < 3; m++) {
        mn_mod *d = &e->mod[m];
        const uint8_t *p = e->pt->p + P_MOD(m);
        int sync = mn_list_index(P_MOD(m) + MO_SYNC, p[MO_SYNC]);
        d->fade_t = 0.0f;
        d->cycles = 0;
        if (sync == SYNC_KEY || sync == SYNC_BPMKEY) {
            d->ph = 0.0f;
            if (mn_list_index(P_MOD(m) + MO_MODE, p[MO_MODE]) == MODE_RND)
                d->target = rnd_bip(&d->rs);
        }
        d->stage = 1;                    /* ENV */
    }
}

/* die Step-Modulatoren: einen Schritt weiter (im Takt der Sequencer-Teilung) */
static void mods_step(mn_engine *e, int restart)
{
    int m;
    for (m = 0; m < 3; m++) {
        mn_mod *d = &e->mod[m];
        int n = mn_list_index(P_MOD(m) + MO_SCOUNT, e->pt->p[P_MOD(m) + MO_SCOUNT]) + 1;
        d->step = restart ? 0 : (d->step + 1) % n;
    }
}

static void mods_run(mn_engine *e, int n)
{
    int m, dd;
    float dt = (float)n / e->sr;
    for (dd = 0; dd < MN_NDEST; dd++)
        e->md[dd] = 0.0f;
    for (m = 0; m < 3; m++) {
        mn_mod *d = &e->mod[m];
        const uint8_t *p = e->pt->p + P_MOD(m);
        int mode = mn_list_index(P_MOD(m) + MO_MODE, p[MO_MODE]);
        int sync = mn_list_index(P_MOD(m) + MO_SYNC, p[MO_SYNC]);
        float out = 0.0f, any = 0.0f;
        for (dd = 0; dd < MN_NDEST; dd++)
            any += mn_abs(e->lvl[m][dd]);
        switch (mode) {
        case MODE_LFO: {
            float hz = mod_hz(e, sync, p[MO_LRATE]), fade = bipn(p[MO_LFADE]), g = 1.0f, ph;
            int wv = mn_list_index(P_MOD(m) + MO_LWAVE, p[MO_LWAVE]);
            d->ph += hz * dt;
            if (d->ph >= 1.0f) {
                d->ph -= (float)(int)d->ph;
                d->cycles++;
                d->chaos = 0.25f + 0.75f * (0.5f + 0.5f * rnd_bip(&d->rs));   /* CHAOS SAW: neue Höhe je Zyklus */
            }
            ph = d->ph;
            switch (wv) {
            case LW_TRI: out = 1.0f - 4.0f * mn_abs(ph - 0.5f); break;
            case LW_SQR: out = ph < 0.5f ? 1.0f : -1.0f; break;
            case LW_PULSE: out = ph < 0.25f ? 1.0f : -1.0f; break;
            case LW_TRAP: out = mn_clamp(3.0f * (1.0f - 4.0f * mn_abs(ph - 0.5f)), -1.0f, 1.0f); break;
            default: out = (1.0f - 2.0f * ph) * (d->chaos > 0.0f ? d->chaos : 1.0f); break;   /* fällt bei + */
            }
            /* FADE: + blendet ein, - blendet aus, ganz links: ein einziger Durchlauf */
            d->fade_t += dt;
            if (fade > 0.02f)
                g = mn_clamp(d->fade_t / (fade * 6.0f), 0.0f, 1.0f);
            else if (fade < -0.98f)
                g = d->cycles == 0 ? 1.0f : 0.0f;
            else if (fade < -0.02f)
                g = mn_clamp(1.0f - d->fade_t / ((1.0f + fade) * 6.0f + 0.05f), 0.0f, 1.0f);
            out *= g;
            break;
        }
        case MODE_ENV: {                 /* A, D/R, S: am Gate der Note */
            float a = mn_env_ms(p[MO_EA], 1) * 0.001f, dr = mn_env_ms(p[MO_EDR], 0) * 0.001f;
            float s = nrm(p[MO_ES]);
            if (d->stage == 1 && !e->gate && d->out <= 0.0f)
                d->stage = 0;
            if (!e->gate && d->stage && d->stage < 3)
                d->stage = 3;
            switch (d->stage) {
            case 1:
                d->out += dt / (a + 1e-4f);
                if (d->out >= 1.0f) {
                    d->out = 1.0f;
                    d->stage = 2;
                }
                break;
            case 2: d->out = s + (d->out - s) * mn_expf(-6.9f * dt / dr); break;
            case 3:
                d->out *= mn_expf(-6.9f * dt / dr);
                if (d->out < 1e-6f)
                    d->out = 0.0f;
                break;
            default: d->out = 0.0f; break;
            }
            out = d->out;
            break;
        }
        case MODE_RND: {                 /* RATE, LAG, PROB: Sample & Hold mit Gleiten */
            float hz = mod_hz(e, sync, p[MO_RRATE]), lag = nrm(p[MO_RLAG]) * nrm(p[MO_RLAG]) * 2.0f;
            d->ph += hz * dt;
            if (d->ph >= 1.0f) {
                d->ph -= (float)(int)d->ph;
                if ((0.5f + 0.5f * rnd_bip(&d->rs)) * 127.0f < (float)p[MO_RPROB])
                    d->target = rnd_bip(&d->rs);
            }
            d->out += (d->target - d->out) * (lag < 0.001f ? 1.0f : 1.0f - mn_expf(-dt / lag));
            out = d->out;
            break;
        }
        default: {                       /* STEP: die Werte im Takt, kurz geglättet (kein Klicken) */
            int n2 = mn_list_index(P_MOD(m) + MO_SCOUNT, p[MO_SCOUNT]) + 1;
            float t = nrm(e->pt->mstep[m][d->step % n2]);
            d->out += (t - d->out) * 0.35f;
            out = d->out;
            break;
        }
        }
        d->held = out;
        if (any > 0.0f)
            for (dd = 0; dd < MN_NDEST; dd++)
                e->md[dd] += out * e->lvl[m][dd];
    }
}

float mn_mod_out(const mn_engine *e, int m) { return m >= 0 && m < 3 ? e->mod[m].held : 0.0f; }
int mn_mod_step(const mn_engine *e, int m) { return m >= 0 && m < 3 ? e->mod[m].step : 0; }
