/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – die FX nach der Stimme, in der Reihenfolge des Typhon: Hochpass und FX1 (Verzerrung), FX2 (Chorus,
 * Ensemble, Flanger), STORM-Breite, FX3 (Delay, Ping Pong, Large / Soft Reverb). Teil von monsun_core.c */

static const int COMB_L[6] = {1116, 1188, 1277, 1356, 1422, 1491}, AP_L[4] = {556, 441, 341, 225};
#define SPREAD 23

void mn_fx_memory(mn_engine *e, int16_t *dl, int16_t *dr, float *cho, float *rev, float *wide)
{
    mn_fx *f = &e->fx;
    int i, at = 0;
    f->ready = 0;
    if (!dl || !dr || !cho || !rev || !wide)
        return;
    for (i = 0; i < 12; i++) {           /* Kämme 0..5 links, 6..11 rechts (etwas länger: Stereo) */
        f->clen[i] = COMB_L[i % 6] + (i >= 6 ? SPREAD : 0);
        f->comb[i] = rev + at;
        at += f->clen[i];
    }
    for (i = 0; i < 8; i++) {
        f->alen[i] = AP_L[i & 3] + (i >= 4 ? SPREAD : 0);
        f->ap[i] = rev + at;
        at += f->alen[i];
    }
    if (at > MN_REV_FLOATS)
        return;
    for (i = 0; i < at; i++)
        rev[i] = 0.0f;
    for (i = 0; i < MN_DLY_FRAMES; i++)
        dl[i] = dr[i] = 0;
    for (i = 0; i < MN_CHO_FRAMES; i++)
        cho[i] = 0.0f;
    for (i = 0; i < MN_WIDE_FRAMES; i++)
        wide[i] = 0.0f;
    f->dl = dl;
    f->dr = dr;
    f->cho = cho;
    f->wide = wide;
    f->ready = 1;
}

static inline float comb_run(mn_fx *f, int c, int len, float x, float fb, float damp)
{
    float y;
    if (f->cpos[c] >= len)               /* SIZE hat den Kamm gekürzt */
        f->cpos[c] = 0;
    y = f->comb[c][f->cpos[c]];
    f->cf[c] = y * (1.0f - damp) + f->cf[c] * damp + 1e-20f;   /* (+1e-20: keine Denormals) */
    f->comb[c][f->cpos[c]] = x + f->cf[c] * fb;
    if (++f->cpos[c] >= len)
        f->cpos[c] = 0;
    return y;
}

static inline float ap_run(mn_fx *f, int a, float x)
{
    float b = f->ap[a][f->apos[a]], y = b - x;
    f->ap[a][f->apos[a]] = x + b * 0.5f + 1e-20f;   /* (+1e-20: keine Denormals) */
    if (++f->apos[a] >= f->alen[a])
        f->apos[a] = 0;
    return y;
}

static inline int16_t to16(float x)
{
    x *= 8192.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}

static inline float fold(float y)
{
    int k;
    for (k = 0; k < 8; k++) {
        if (y > 1.0f)
            y = 2.0f - y;
        else if (y < -1.0f)
            y = -2.0f - y;
        else
            break;
    }
    return mn_clamp(y, -1.0f, 1.0f);
}

static inline float dist(mn_fx *f, int type, float x, float g, float lvl, float bits_q, int hold)
{
    float y = x * g;
    switch (type) {
    case X1_HARD: y = mn_clamp(y, -1.0f, 1.0f); break;
    case X1_FOLD: y = fold(y); break;
    case X1_SCURVE: y = mn_soft(y); break;
    case X1_TRI: {                       /* ein periodisches Dreieck: faltet immer weiter */
        float t = y * 0.25f + 0.25f;
        t -= (float)(int)t;
        if (t < 0.0f)
            t += 1.0f;
        y = 1.0f - 4.0f * mn_abs(t - 0.5f);
        break;
    }
    case X1_SHRED:                       /* gleichgerichtet und gefaltet: rau, kaputt */
        y = mn_soft(y);
        y = fold((y > 0.0f ? y : -0.7f * y) * 2.6f - 0.6f);
        break;
    default:                             /* BIT CRUSH: GAIN = Samplerate, LVL = Bittiefe */
        if (++f->cr_n >= hold) {
            f->cr_n = 0;
            f->cr_v = (float)(int32_t)(x * bits_q) / bits_q;
        }
        return f->cr_v;
    }
    return y * lvl;
}

/* die Delay-Zeit: frei 10 ms .. 1 s, oder (SYNC) eine Teilung des Tempos (1/2 .. 1/64), höchstens 1 s */
static float dly_samples(const mn_engine *e, int v, int sync)
{
    float s;
    if (sync) {
        int i = 3 + (v * (MN_NDIV - 1 - 3) + 63) / 127;
        s = e->sr * 60.0f / mn_bpm(e) * MN_DIV_BEATS[i];
    } else {
        s = e->sr * 0.01f * mn_exp2((float)v / 127.0f * 6.64f);
    }
    return mn_clamp(s, 16.0f, (float)(MN_DLY_FRAMES - 4));
}

static inline float dly_read(const int16_t *b, float rp)
{
    int p0, p1;
    float fr;
    while (rp < 0.0f)
        rp += (float)MN_DLY_FRAMES;
    p0 = (int)rp;
    fr = rp - (float)p0;
    if (p0 >= MN_DLY_FRAMES)
        p0 -= MN_DLY_FRAMES;
    p1 = p0 + 1 >= MN_DLY_FRAMES ? 0 : p0 + 1;
    return ((float)b[p0] + ((float)b[p1] - (float)b[p0]) * fr) * (1.0f / 8192.0f);
}

static inline float ring_read(const float *b, int len, int wpos, float d)
{
    float rp = (float)wpos - d;
    int p0, p1;
    float fr;
    while (rp < 0.0f)
        rp += (float)len;
    p0 = (int)rp;
    fr = rp - (float)p0;
    if (p0 >= len)
        p0 -= len;
    p1 = p0 + 1 >= len ? 0 : p0 + 1;
    return b[p0] + (b[p1] - b[p0]) * fr;
}

static void fx_render(mn_engine *e, const float *in, float *l, float *r, int n)
{
    mn_fx *f = &e->fx;
    const uint8_t *p = e->pt->p;
    const float *md = e->md;
    float sr = e->sr;
    int i;
    /* FX1 */
    int x1 = mn_list_index(P_X1TYPE, p[P_X1TYPE]);
    float gn = nrm(p[P_X1GAIN]), g1 = 1.0f + gn * gn * 40.0f, lv1 = nrm(p[P_X1LVL]);
    float mix1 = mn_clamp(nrm(p[P_X1MIX]) + md[D_FX1], 0.0f, 1.0f);
    float bq = mn_exp2(1.0f + lv1 * 14.0f);                 /* CRUSH: 2 .. 16 Bit */
    int hold = 1 + (int)(gn * 31.0f);                       /* CRUSH: die Samplerate bis 1/32 */
    /* der Hochpass (nach dem VCA, vor den FX; die Koeffizienten einmal pro Block) */
    float hp_oct = (e->u_hp > 0.0f || md[D_HPF] != 0.0f) ? mn_clamp(e->u_hp + md[D_HPF] * 5.0f, 0.0f, 10.0f) : 0.0f;
    float hf = 0.0f, hq = 0.0f;
    /* FX2 */
    int x2 = mn_list_index(P_X2TYPE, p[P_X2TYPE]);
    float mix2 = mn_clamp(nrm(p[P_X2MIX]) + md[D_FX2], 0.0f, 1.0f);
    float spd = 0.03f * mn_exp2(nrm(p[P_X2SPD]) * 8.0f), dep = nrm(p[P_X2DEPTH]), wid = nrm(p[P_X2WIDTH]);
    float fb2 = bipn(p[P_X2FEED]) * (x2 == X2_FLANGER ? 0.92f : 0.6f);
    float base = x2 == X2_FLANGER ? 0.0006f : x2 == X2_ENSEMBLE ? 0.009f : 0.007f;
    float span = x2 == X2_FLANGER ? 0.004f : x2 == X2_ENSEMBLE ? 0.003f : 0.005f;
    /* STORM */
    float st = e->u_storm;
    /* FX3 */
    int x3 = mn_list_index(P_X3TYPE, p[P_X3TYPE]);
    float mix3 = mn_clamp(nrm(p[P_X3MIX]) + md[D_FX3], 0.0f, 1.0f);
    float fb3 = nrm(p[P_X3FEED]) * 0.95f, damp = bipn(p[P_X3DAMP]);
    int sync3 = p[P_X3SYNC] >= 64, rev = x3 >= X3_LARGE;
    int nc = x3 == X3_LARGE ? 6 : 4, na = x3 == X3_LARGE ? 4 : 2;
    float tl = 0.0f, tr = 0.0f, lpc = 1.0f, hpc = 0.0f, rfb = 0.0f, cdamp = 0.2f, size = 1.0f, pre = 0.0f;
    int len[12], c;
    if (!f->ready) {
        for (i = 0; i < n; i++)
            l[i] = r[i] = mn_soft(in[i]);
        return;
    }
    {   /* ausklingende Zustände auf 0 (keine Denormals: auf manchen Prozessoren sehr langsam) */
        float *z[9] = {&f->hp_l, &f->hp_b, &f->cho_fb, &f->dfl, &f->dfr, &f->dhl, &f->dhr, &f->rdh, &f->rdh2};
        for (c = 0; c < 9; c++)
            if (mn_abs(*z[c]) < 1e-15f)
                *z[c] = 0.0f;
    }
    if (hp_oct > 0.0f) {
        /* ein TPT-SVF (Zavalishin): stabil bis an die Nyquist-Grenze */
        float fc = mn_clamp(20.0f * mn_exp2(hp_oct), 20.0f, 0.45f * sr), w = MN_PI * fc / sr;
        hf = mn_sin(w) / mn_sin(0.5f * MN_PI - w);   /* tan(w) */
        hq = 1.9f - 1.6f * e->u_hpq;     /* resonant, aber er schwingt nicht */
    }
    /* DAMP: - ein Tiefpass, + ein Hochpass (in den Echos, auf dem Hall) */
    if (damp < -0.02f)
        lpc = mn_clamp(1.0f + damp * 0.94f, 0.06f, 1.0f);
    if (damp > 0.02f)
        hpc = mn_clamp(2.0f * MN_PI * 20.0f * mn_exp2(damp * 7.0f) / sr, 0.0f, 0.5f);
    if (!rev) {
        tl = dly_samples(e, p[P_X3P1], sync3);
        tr = x3 == X3_DELAY ? dly_samples(e, p[P_X3P2], sync3) : tl;
        if (f->dsl <= 0.0f) {
            f->dsl = tl;
            f->dsr = tr;
        }
    } else {
        rfb = x3 == X3_LARGE ? 0.72f + 0.27f * nrm(p[P_X3FEED]) : 0.62f + 0.26f * nrm(p[P_X3FEED]);
        cdamp = x3 == X3_LARGE ? 0.2f : 0.4f;
        if (damp < -0.02f)
            cdamp += -damp * 0.5f;
        size = 0.4f + 0.6f * nrm(p[P_X3P2]);
        pre = nrm(p[P_X3P1]) * 0.1f * sr;   /* PRE: bis 100 ms */
        for (c = 0; c < 12; c++)
            len[c] = (int)((float)f->clen[c] * size);
    }
    for (i = 0; i < n; i++) {
        float x = in[i], L, R;
        /* ---- Hochpass ---- */
        if (hp_oct > 0.0f) {
            float a1 = 1.0f / (1.0f + hf * (hf + hq)), a2 = hf * a1, a3 = hf * a2;
            float v3 = x - f->hp_l, v1 = a1 * f->hp_b + a2 * v3, v2 = f->hp_l + a2 * f->hp_b + a3 * v3;
            f->hp_b = 2.0f * v1 - f->hp_b;   /* (hp_b, hp_l: die zwei Integratoren) */
            f->hp_l = 2.0f * v2 - f->hp_l;
            x = x - hq * v1 - v2;
        }
        /* ---- FX1 ---- */
        if (mix1 > 0.001f)
            x += (dist(f, x1, x, g1, lv1, bq, hold) - x) * mix1;
        L = R = x;
        /* ---- FX2 ---- */
        if (mix2 > 0.001f) {
            float wl, wr, ph = f->cho_ph, phr = ph + wid * 0.5f;
            if (phr >= 1.0f)
                phr -= 1.0f;
            f->cho[f->cho_pos] = mn_soft(x + f->cho_fb * fb2) + 1e-20f;
            if (x2 == X2_ENSEMBLE) {     /* drei Stimmen je Seite, um ein Drittel versetzt, und ein schnelles Flimmern */
                float fl = ph * 7.0f, shim;
                int t;
                fl -= (float)(int)fl;
                shim = 0.0003f * (1.0f - 4.0f * mn_abs(fl - 0.5f));
                wl = wr = 0.0f;
                for (t = 0; t < 3; t++) {
                    float a = ph + (float)t * 0.3333333f, b = phr + (float)t * 0.3333333f;
                    a -= (float)(int)a;
                    b -= (float)(int)b;
                    wl += ring_read(f->cho, MN_CHO_FRAMES, f->cho_pos, (base + span * dep * (1.0f - 2.0f * mn_abs(a - 0.5f)) + shim) * sr);
                    wr += ring_read(f->cho, MN_CHO_FRAMES, f->cho_pos, (base + span * dep * (1.0f - 2.0f * mn_abs(b - 0.5f)) - shim) * sr);
                }
                wl *= 0.4f;
                wr *= 0.4f;
            } else {
                wl = ring_read(f->cho, MN_CHO_FRAMES, f->cho_pos, (base + span * dep * (1.0f - 2.0f * mn_abs(ph - 0.5f))) * sr + 1.0f);
                wr = ring_read(f->cho, MN_CHO_FRAMES, f->cho_pos, (base + span * dep * (1.0f - 2.0f * mn_abs(phr - 0.5f))) * sr + 1.0f);
            }
            f->cho_fb = 0.5f * (wl + wr);
            if (++f->cho_pos >= MN_CHO_FRAMES)
                f->cho_pos = 0;
            f->cho_ph += spd / sr;
            if (f->cho_ph >= 1.0f)
                f->cho_ph -= 1.0f;
            if (x2 == X2_FLANGER) {      /* der Flanger lebt vom Kammfilter: trocken plus nass */
                L = x * (1.0f - 0.45f * mix2) + wl * mix2 * 0.75f;
                R = x * (1.0f - 0.45f * mix2) + wr * mix2 * 0.75f;
            } else {
                L = x + (wl - x) * mix2;
                R = x + (wr - x) * mix2;
            }
        }
        /* ---- STORM: ein langsam wandernder Seitenkanal macht das Bild breit ---- */
        if (st > 0.01f) {
            float d;
            f->wide[f->wpos] = 0.5f * (L + R);
            f->wph += 0.21f / sr;
            if (f->wph >= 1.0f)
                f->wph -= 1.0f;
            d = ring_read(f->wide, MN_WIDE_FRAMES, f->wpos, (0.008f + 0.006f * (1.0f - 2.0f * mn_abs(f->wph - 0.5f))) * sr);
            if (++f->wpos >= MN_WIDE_FRAMES)
                f->wpos = 0;
            L += st * 0.55f * d;
            R -= st * 0.55f * d;
        }
        /* ---- FX3 ---- */
        if (mix3 > 0.001f) {
            float wl, wr;
            if (!rev) {
                float yl, yr, il, ir;
                f->dsl += (tl - f->dsl) * 0.0005f;
                f->dsr += (tr - f->dsr) * 0.0005f;
                yl = dly_read(f->dl, (float)f->dpos - f->dsl);
                yr = dly_read(f->dr, (float)f->dpos - f->dsr);
                f->dfl += lpc * (yl - f->dfl);           /* der Dämpfer im Rücklauf */
                f->dfr += lpc * (yr - f->dfr);
                f->dhl += hpc * (f->dfl - f->dhl);
                f->dhr += hpc * (f->dfr - f->dhr);
                il = f->dfl - f->dhl;
                ir = f->dfr - f->dhr;
                if (x3 == X3_DELAY) {
                    f->dl[f->dpos] = to16(mn_soft(L + il * fb3));
                    f->dr[f->dpos] = to16(mn_soft(R + ir * fb3));
                } else {                 /* Ping Pong: links der Eingang und das rechte Echo, rechts das linke */
                    f->dl[f->dpos] = to16(mn_soft(0.5f * (L + R) + ir * fb3));
                    f->dr[f->dpos] = to16(il);
                }
                if (++f->dpos >= MN_DLY_FRAMES)
                    f->dpos = 0;
                L = L * (1.0f - 0.4f * mix3) + yl * mix3;
                R = R * (1.0f - 0.4f * mix3) + yr * mix3;
            } else {
                float ri, sl = 0.0f, srr = 0.0f;
                f->dl[f->dpos] = to16(0.5f * (L + R));   /* PRE im Speicher des Delays (das ruht derweil) */
                ri = dly_read(f->dl, (float)f->dpos - pre - 1.0f) * 0.03f;
                if (++f->dpos >= MN_DLY_FRAMES)
                    f->dpos = 0;
                for (c = 0; c < nc; c++) {
                    sl += comb_run(f, c, len[c], ri, rfb, cdamp);
                    srr += comb_run(f, 6 + c, len[6 + c], ri, rfb, cdamp);
                }
                for (c = 0; c < na; c++) {
                    sl = ap_run(f, c, sl);
                    srr = ap_run(f, 4 + c, srr);
                }
                if (hpc > 0.0f) {        /* + DAMP: ein Hochpass auf dem Hall */
                    f->rdh += hpc * (sl - f->rdh);
                    f->rdh2 += hpc * (srr - f->rdh2);
                    sl -= f->rdh;
                    srr -= f->rdh2;
                }
                wl = sl * (x3 == X3_LARGE ? 0.9f : 1.2f);
                wr = srr * (x3 == X3_LARGE ? 0.9f : 1.2f);
                L += (wl - L) * mix3 * 0.5f + wl * mix3 * 0.5f;
                R += (wr - R) * mix3 * 0.5f + wr * mix3 * 0.5f;
            }
        }
        l[i] = mn_soft(L);
        r[i] = mn_soft(R);
    }
}
