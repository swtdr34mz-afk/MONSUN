/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – die Stimme: zwei VCOs mit dem WAVE-Regler, der Ladder, die Hüllkurven, STORM. Teil von monsun_core.c */

/* ---- Oszillator: Dreieck, Säge, Rechteck, schmaler Puls (PolyBLEP); die Form stufenlos 0..3 ---- */
static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline float osc_step(mn_osc *o, float inc)
{
    o->phase += inc;
    if (o->phase >= 1.0f)
        o->phase -= (float)(int)o->phase;
    else if (o->phase < 0.0f) {
        o->phase += 1.0f + (float)(int)(-o->phase);
        if (o->phase >= 1.0f)
            o->phase -= 1.0f;
    }
    return o->phase;
}

static inline float shape_one(float ph, float dt, int s)
{
    float y, t2, pw;
    switch (s) {
    case 0: return 1.0f - 4.0f * mn_abs(ph - 0.5f);   /* Dreieck (am 2x Oversampling sauber genug) */
    case 1: return 2.0f * ph - 1.0f - blep(ph, dt);   /* Säge */
    default:
        pw = s == 2 ? 0.5f : 0.16f;                  /* Rechteck, schmaler Puls */
        y = ph < pw ? 1.0f : -1.0f;
        y += blep(ph, dt);
        t2 = ph - pw;
        if (t2 < 0.0f)
            t2 += 1.0f;
        y -= blep(t2, dt);
        return y - (2.0f * pw - 1.0f);               /* ohne Gleichanteil */
    }
}

static inline float shape(float ph, float inc, float s)
{
    float dt = mn_clamp(mn_abs(inc), 1e-6f, 0.5f), f;
    int i = (int)s;
    if (i >= 3)
        return shape_one(ph, dt, 3);
    f = s - (float)i;
    if (f < 0.02f)
        return shape_one(ph, dt, i);
    if (f > 0.98f)
        return shape_one(ph, dt, i + 1);
    return shape_one(ph, dt, i) * (1.0f - f) + shape_one(ph, dt, i + 1) * f;
}

/* ---- die WAVE-Stellungen: Form und Pegel beider VCOs, und wie stark VCO 2 VCO 1 moduliert (FM).
 *      Der Regler blendet zwischen benachbarten Stellungen, er schaltet nicht. ---- */
typedef struct { float s1, l1, s2, l2, fm; } wave_pos;
static const wave_pos WAVES[MN_NWAVES] = {
    {0.0f, 1.0f, 0.0f, 0.0f, 0.0f},    /* TRI */
    {1.0f, 1.0f, 1.0f, 0.0f, 0.0f},    /* SAW */
    {1.0f, 1.0f, 1.0f, 1.0f, 0.0f},    /* 2 SAW */
    {1.0f, 1.0f, 2.0f, 1.0f, 0.0f},    /* SAW+SQR */
    {2.0f, 1.0f, 2.0f, 1.0f, 0.0f},    /* 2 SQR */
    {3.0f, 1.0f, 2.0f, 0.7f, 0.0f},    /* PULSE */
    {0.0f, 1.0f, 0.0f, 0.0f, 0.45f},   /* FM SOFT: VCO 2 (Dreieck) moduliert VCO 1 */
    {0.0f, 1.0f, 0.0f, 0.25f, 1.4f},   /* FM HARD */
};

static void wave_at(float pos, wave_pos *w)
{
    int i;
    float f;
    pos = mn_clamp(pos, 0.0f, (float)(MN_NWAVES - 1));
    i = (int)pos;
    if (i >= MN_NWAVES - 1) {
        *w = WAVES[MN_NWAVES - 1];
        return;
    }
    f = pos - (float)i;
    w->s1 = WAVES[i].s1 + (WAVES[i + 1].s1 - WAVES[i].s1) * f;
    w->l1 = WAVES[i].l1 + (WAVES[i + 1].l1 - WAVES[i].l1) * f;
    w->s2 = WAVES[i].s2 + (WAVES[i + 1].s2 - WAVES[i].s2) * f;
    w->l2 = WAVES[i].l2 + (WAVES[i + 1].l2 - WAVES[i].l2) * f;
    w->fm = WAVES[i].fm + (WAVES[i + 1].fm - WAVES[i].fm) * f;
}

/* ---- der Ladder nach Huovilainen (aus RAND): vier sättigende Stufen, mit doppelter Samplerate ---- */
static inline float ladder_run(mn_ladder *f, float x, float g, float k)
{
    float u = mn_soft(x - k * f->last), t3;
    f->s0 += g * (u - f->t0);
    f->t0 = mn_soft(f->s0);
    f->s1 += g * (f->t0 - f->t1);
    f->t1 = mn_soft(f->s1);
    f->s2 += g * (f->t1 - f->t2);
    f->t2 = mn_soft(f->s2);
    t3 = mn_soft(f->s3);
    f->s3 += g * (f->t2 - t3);
    f->last = 0.5f * (f->s3 + f->p3);
    f->p3 = f->s3;
    return f->s3;
}

static inline void ladder_coefs(float fc, float res, float os_inv, float *g, float *k)
{
    float fcr, acr;
    fc *= 1.0f + 0.011f * res;
    fcr = 1.8730f * fc * fc * fc + 0.4955f * fc * fc - 0.6490f * fc + 0.9988f;
    acr = -3.9364f * fc * fc + 1.8409f * fc + 0.9968f;
    *g = 1.0f - mn_exp2(-2.0f * MN_PI * os_inv * fc * fcr * 1.44269504f);
    *k = 4.1f * res * acr;
}

/* der Halbband-Dezimierer (2x Oversampling zurück), aus RAND */
#define HB_N 23
static const float HB_H[6] = {0.003001f, -0.011798f, 0.033245f, -0.084613f, 0.310479f, 0.499918f};
static inline float hb_push(mn_engine *v, float a, float b)
{
    float *h;
    int p = v->hb_pos;
    v->hb[p] = v->hb[p + HB_N] = a;
    p = p + 1 >= HB_N ? 0 : p + 1;
    v->hb[p] = v->hb[p + HB_N] = b;
    p = p + 1 >= HB_N ? 0 : p + 1;
    v->hb_pos = p;
    h = v->hb + p;
    return HB_H[5] * h[11] + HB_H[4] * (h[10] + h[12]) + HB_H[3] * (h[8] + h[14]) + HB_H[2] * (h[6] + h[16])
           + HB_H[1] * (h[4] + h[18]) + HB_H[0] * (h[2] + h[20]) - 0.000272f * (h[0] + h[22]);
}

/* ---- ADSR: linearer Anstieg, exponentielles Abfallen; Neustart vom aktuellen Pegel (kein Knacksen) ---- */
static float time_scale(float bip)       /* TIME-Regler: bis 10x langsamer (links), bis 6x schneller (rechts) */
{
    return bip < 0.0f ? mn_exp2(-bip * 3.3219f) : mn_exp2(-bip * 2.585f);
}

static void adsr_set(mn_adsr *a, int va, int vd, int vs, int vr, float scale, float sr)
{
    float ta = mn_env_ms(va, 1) * 0.001f * scale, td = mn_env_ms(vd, 0) * 0.001f * scale;
    float tr = mn_env_ms(vr, 0) * 0.001f * scale;
    a->a_inc = 1.0f / (mn_clamp(ta, 0.0005f, 60.0f) * sr);
    a->d_coef = mn_expf(-6.9f / (mn_clamp(td, 0.001f, 90.0f) * sr));
    a->r_coef = mn_expf(-6.9f / (mn_clamp(tr, 0.001f, 90.0f) * sr));
    a->sus = (float)vs / 127.0f;
}

static inline float adsr_run(mn_adsr *a)
{
    switch (a->stage) {
    case 1:
        a->level += a->a_inc;
        if (a->level >= 1.0f) {
            a->level = 1.0f;
            a->stage = 2;
        }
        break;
    case 2:
        a->level = a->sus + (a->level - a->sus) * a->d_coef;
        if (a->level < 1e-6f)            /* (keine Denormals, wenn S = 0) */
            a->level = 0.0f;
        break;
    case 3:
        a->level *= a->r_coef;
        if (a->level < 1e-5f) {
            a->level = 0.0f;
            a->stage = 0;
        }
        break;
    default: break;
    }
    return a->level;
}

/* ---- umrechnen, wenn sich der Klang geändert hat ---- */
static float nrm(int v) { return (float)v * (1.0f / 127.0f); }
static float bipn(int v) { return mn_clamp(((float)v - 64.0f) / 63.0f, -1.0f, 1.0f); }

static void voice_update(mn_engine *e)
{
    const uint8_t *p = e->pt->p;
    int m, d;
    e->u_wave = nrm(p[P_WAVE]) * (float)(MN_NWAVES - 1);
    e->u_tune2 = mn_tune2_semis(p[P_TUNE2]);
    e->u_cut = nrm(p[P_CUTOFF]) * 10.0f;               /* Oktaven über 20 Hz */
    e->u_res = nrm(p[P_RESON]);
    e->u_egl = bipn(p[P_EGLVL]);
    e->u_track = nrm(p[P_TRACK]) * 2.0f;               /* Mitte = 100 % */
    e->u_ffm = nrm(p[P_FFM]);
    e->u_vco = 0.25f + nrm(p[P_VCOLVL]) * 0.75f;
    e->u_vca = nrm(p[P_VCALVL]);
    e->u_velcut = nrm(p[P_VELCUT]) * 3.0f;
    e->u_velvca = nrm(p[P_VELVCA]);
    e->u_glide = mn_glide_ms(p[P_GLIDE]);
    e->legato = p[P_LEGATO] >= 64;
    e->auto_glide = mn_list_index(P_GMODE, p[P_GMODE]) == 1;
    e->u_storm = nrm(p[P_STORM]);
    e->u_hp = p[P_HPCUT] ? 0.01f + nrm(p[P_HPCUT]) * 10.0f : 0.0f;    /* 0: aus; sonst 20 Hz * 2^u (bis 7 kHz) */
    e->u_hpq = nrm(p[P_HPRES]);
    adsr_set(&e->fe, p[P_FA], p[P_FD], p[P_FS], p[P_FR], time_scale(bipn(p[P_FTIME])), e->sr);
    adsr_set(&e->ae, p[P_AA], p[P_AD], p[P_AS], p[P_AR], time_scale(bipn(p[P_ATIME])), e->sr);
    for (m = 0; m < 3; m++)
        for (d = 0; d < MN_NDEST; d++) {
            float l = bipn(e->pt->lvl[m][d]);
            if (d <= D_CV2)                  /* Tonhöhe: fein um 0 (Vibrato), bis ±24 Halbtöne am Rand */
                l = l * mn_abs(l);
            e->lvl[m][d] = l;
        }
    e->dirty = 0;
}

/* ---- Noten an die Stimme ---- */
static void mods_key(mn_engine *e);

static void voice_on(mn_engine *e, int note, float vel, int from_seq_tie)
{
    int legato_now = e->gate && (e->legato || from_seq_tie);
    int glide = e->u_glide > 0.0f && (!e->auto_glide || e->gate);
    e->pitch_to = (float)note;
    if (!glide || e->note < 0)
        e->pitch = e->pitch_to;
    e->glide_coef = glide ? mn_expf(-1.0f / (e->u_glide * 0.001f * e->sr / 8.0f + 1.0f)) : 0.0f;
    e->note = note;
    if (!legato_now) {
        e->vel = vel;
        e->fe.stage = e->ae.stage = 1;
        mods_key(e);
    }
    e->gate = 1;
}

static void voice_off(mn_engine *e)
{
    e->gate = 0;
    if (e->fe.stage)
        e->fe.stage = 3;
    if (e->ae.stage)
        e->ae.stage = 3;
}

/* ---- die Stimme: n Samples, in Stücken zu 8 (die Modulatoren rechnen pro Stück) ---- */
static void mods_run(mn_engine *e, int n);

static void voice_render(mn_engine *e, float *out, int n)
{
    int i, os = e->eco ? 1 : 2;
    float inv = 1.0f / ((float)os * e->sr), fmax = mn_clamp(0.45f * e->sr, 1000.0f, 20000.0f);
    if (e->dirty)
        voice_update(e);
    for (i = 0; i < n; i += 8) {
        int j, c = n - i < 8 ? n - i : 8;
        const float *md = e->md;
        float pitch, f1, f2, inc1, inc2, inc3, inc4, det, cut, res, ffm, amp_base, velg, egl, comp = 1.0f, g = 0.0f, k = 0.0f;
        float st = e->u_storm, mixg;
        wave_pos w;
        mods_run(e, c);
        /* Glide, einmal pro Stück */
        e->pitch = e->pitch_to + (e->pitch - e->pitch_to) * e->glide_coef;
        pitch = e->pitch + e->bend + md[D_CV] * 24.0f;
        /* die VCOs wandern ein paar Cent, jeder für sich */
        e->rs ^= e->rs << 13;
        e->rs ^= e->rs >> 17;
        e->rs ^= e->rs << 5;
        e->drift1 += 2e-4f * ((float)(int32_t)e->rs * (1.0f / 2147483648.0f)) - 2e-3f * e->drift1;
        e->drift2 += 2e-4f * ((float)(int32_t)(e->rs * 2654435761u) * (1.0f / 2147483648.0f)) - 2e-3f * e->drift2;
        f1 = 440.0f * mn_exp2((pitch + md[D_CV1] * 24.0f - 69.0f) * (1.0f / 12.0f) + e->drift1);
        f2 = 440.0f * mn_exp2((pitch + e->u_tune2 + md[D_CV2] * 24.0f - 69.0f) * (1.0f / 12.0f) + e->drift2);
        inc1 = mn_clamp(f1 * inv, 0.0f, 0.45f);
        inc2 = mn_clamp(f2 * inv, 0.0f, 0.45f);
        det = mn_exp2(st * 0.03f + 0.002f);          /* STORM: die Kopien bis ~35 Cent daneben */
        inc3 = mn_clamp(inc1 * det, 0.0f, 0.45f);
        inc4 = mn_clamp(inc2 / det, 0.0f, 0.45f);
        wave_at(e->u_wave + md[D_WAVE] * (float)(MN_NWAVES - 1), &w);
        mixg = e->u_vco * 0.62f / (1.0f + 0.55f * w.l2 * (w.fm < 0.3f ? 1.0f : 0.0f)) / (1.0f + st * 0.7f);
        /* der Filter: Cutoff, Key-Track, Hüllkurve, Velocity, Modulatoren; STORM lässt ihn leicht wandern */
        e->storm_wob += (((float)(int32_t)(e->rs >> 1) * (1.0f / 1073741824.0f)) - e->storm_wob) * 0.002f;
        egl = mn_clamp(e->u_egl + md[D_EGL], -1.0f, 1.0f) * 7.0f;
        cut = e->u_cut + e->u_track * (pitch - 60.0f) * (1.0f / 12.0f) + e->u_velcut * (e->vel - 0.6f)
              + md[D_CUT] * 5.0f + st * e->storm_wob * 0.6f;
        res = mn_clamp(e->u_res + md[D_RES], 0.0f, 1.0f);
        ffm = mn_clamp(e->u_ffm + md[D_FFM], 0.0f, 1.0f) * 3.5f;
        velg = 1.0f - e->u_velvca + e->u_velvca * e->vel;
        amp_base = e->u_vca * 1.15f;
        for (j = 0; j < c; j++) {
            float fe = adsr_run(&e->fe), ae = adsr_run(&e->ae), amp, y = 0.0f, ya = 0.0f;
            int sub;
            float tri2 = 1.0f - 4.0f * mn_abs(e->o2.phase - 0.5f);
            if (!(j & 1)) {                  /* der Filter alle zwei Samples neu (22 kHz Steuerrate) */
                float fc = 20.0f * mn_exp2(cut + egl * fe + ffm * tri2);
                fc = mn_clamp(fc, 15.0f, fmax);
                ladder_coefs(fc / e->sr, res, 1.0f / (float)os, &g, &k);
                comp = 1.0f + k * 0.12f;
            }
            for (sub = 0; sub < os; sub++) {
                float o2v, o1v, ph1, ph2, mix;
                ph2 = osc_step(&e->o2, inc2);
                o2v = w.fm > 0.001f ? 1.0f - 4.0f * mn_abs(ph2 - 0.5f) : 0.0f;
                ph1 = osc_step(&e->o1, mn_clamp(inc1 * (1.0f + w.fm * 2.2f * o2v), -0.45f, 0.45f));
                o1v = shape(ph1, inc1, w.s1);
                mix = o1v * w.l1;
                if (w.l2 > 0.001f)
                    mix += shape(ph2, inc2, w.s2) * w.l2;
                if (st > 0.01f) {            /* STORM: je eine verstimmte Kopie */
                    float p3 = osc_step(&e->o3, inc3), p4 = osc_step(&e->o4, inc4);
                    mix += st * (shape(p3, inc3, w.s1) * w.l1 + (w.l2 > 0.001f ? shape(p4, inc4, w.s2) * w.l2 : 0.0f));
                }
                y = ladder_run(&e->vcf, mix * mixg * comp * 1.6f + 1e-9f, g, k);   /* (+1e-9: keine Denormals) */
                if (!sub)
                    ya = y;
            }
            if (os == 2)
                y = hb_push(e, ya, y);
            amp = mn_clamp(ae * velg + md[D_VCA], 0.0f, 1.5f) * amp_base;
            y *= amp;
            {   /* DC-Blocker */
                float d = y - e->dc_x + 0.995f * e->dc_y;
                e->dc_x = y;
                e->dc_y = mn_abs(d) < 1e-15f ? 0.0f : d;   /* (keine Denormals) */
                out[i + j] = d;
            }
        }
        e->level_now = e->ae.level;
        e->cut_hz = 20.0f * mn_exp2(mn_clamp(cut + egl * e->fe.level, 0.0f, 10.0f));
    }
}
