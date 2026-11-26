/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – GROOVE: zehn Grooves aus einer eigenen Kick, Clap / Snare, Hi-Hats und Perc, alles hier synthetisiert, und
 * die pumpende Sidechain: jede Kick duckt MONSUN (mit Delay und Hall) weg, auch wenn die Drums stumm sind.
 * Läuft mit PLAY (und der MIDI-Clock) im Raster der Sechzehntel. Teil von monsun_core.c */

const char *const MN_STYLE_NAME[MN_NSTYLES] = {"HOUSE", "TECHHOUSE", "TECHNO", "TRANCE", "ACID", "DNB", "DUBSTEP",
                                               "GLITCHPOP", "EDM", "IDM"};

/* die Muster: zwei Takte in Sechzehnteln. '.' nichts, 'g' leise (Ghost), 'x' normal, 'X' betont, 'r' ein Wirbel
 * (zwei Schläge im Step). Spuren: Kick, Clap/Snare, Hat zu, Hat offen, Perc */
typedef struct {
    const char *k, *s, *h, *o, *p;
    float swing;                         /* 0 .. 0.25: die zweite Sechzehntel kommt später */
    uint8_t snare;                       /* 0 Clap, 1 Snare */
    float ktune, kdec, kclick, kdrive;   /* die Kick des Styles (Hz, s, 0..1, 0..1) */
    uint8_t perc;                        /* 0 Rim (hoch, kurz), 1 Blip (Tonhöhe zufällig), 2 Tom */
} style_t;

static const style_t STYLES[MN_NSTYLES] = {
    {"X...X...X...X...X...X...X...X...", "....X.......X.......X.......X...",
     "x.g.x.g.x.g.x.g.x.g.x.g.x.g.x.gx", "..X...X...X...X...X...X...X...X.", "................................",
     0.10f, 0, 54.0f, 0.38f, 0.45f, 0.35f, 0},                                    /* HOUSE */
    {"X...X...X...X...X...X...X...X...", "....X.......X.......X.......X...",
     "xgxgxgxgxgxgxgxgxgxgxgxgxgxgxgxg", "..x...x...x...x...x...x...x...x.", "...x.....x.x...x...x.....x...x.x",
     0.12f, 0, 51.0f, 0.32f, 0.55f, 0.45f, 0},                                    /* TECHHOUSE */
    {"X...X...X...X...X...X...X...X...", "....g.......x.......g.......x...",
     "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..X...X...X...X...X...X...X...X.", "..........x.............x....x..",
     0.0f, 0, 47.0f, 0.55f, 0.35f, 0.75f, 2},                                     /* TECHNO */
    {"X...X...X...X...X...X...X...X...", "....X.......X.......X.......X...",
     "g.g.g.g.g.g.g.g.g.g.g.g.g.g.g.g.", "..X...X...X...X...X...X...X...X.", "................................",
     0.0f, 0, 50.0f, 0.30f, 0.65f, 0.45f, 0},                                     /* TRANCE */
    {"X...X...X...X...X...X...X...X.x.", "....X.......X.......X.......X...",
     "xgxgxgxgxgxgxgxgxgxgxgxgxgxgxgxg", "..x.......x.......x.......x...x.", "................................",
     0.06f, 0, 52.0f, 0.35f, 0.50f, 0.50f, 0},                                    /* ACID */
    {"X.........X.....X.x.......X.....", "....X.......X.......X.......X...",
     "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..............x...............x.", ".......g..g..........g.....g..g",
     0.0f, 1, 58.0f, 0.24f, 0.80f, 0.40f, 1},                                     /* DNB */
    {"X.........x.....X.....X.x.......", "........X...............X.......",
     "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..............X...............X.", "...........x..........x....x....",
     0.0f, 1, 45.0f, 0.55f, 0.45f, 0.70f, 2},                                     /* DUBSTEP */
    {"X......X..X.....X......X.X....x.", "....X.......X.......X.......X.X.",
     "x.xrx.x.x.xrx.x.x.xrx.x.x.x.rrrr", "......x...............x.........", ".x.....x..x...x..x..g....x..x...",
     0.04f, 0, 60.0f, 0.20f, 0.90f, 0.40f, 1},                                    /* GLITCHPOP */
    {"X...X...X...X...X...X...X...X...", "....X.......X.......X.......X...",
     "g.g.g.g.g.g.g.g.g.g.g.g.g.g.g.g.", "..X...X...X...X...X...X...X...X.", "................................",
     0.0f, 0, 48.0f, 0.45f, 0.70f, 0.80f, 0},                                     /* EDM */
    {"X..x....X.x...x.X....x..X..x.x..", "....X..g....X.g.....X.x..g.X....",
     "xrx.xx.rx.xxr.x.xrxx.x.rx.x.xrrr", ".....x.........x.........x......", "..x..x.x...x..x..x..x...x.x..x.x",
     0.0f, 1, 65.0f, 0.18f, 0.85f, 0.55f, 1},                                     /* IDM */
};

void mn_groove_init(mn_groove *g)
{
    g->on = 0;
    g->style = 0;
    g->kick = 64;
    g->drums = 100;
    g->pump = 90;
    g->rel = 60;
    g->hats = 90;
    g->tune = 64;
    g->var = 0;
}

void mn_groove_use(mn_engine *e, const mn_groove *g) { e->gv = g; }

static int grv_style(const mn_engine *e)
{
    int s = e->gv ? e->gv->style : 0;
    return s < 0 ? 0 : s >= MN_NSTYLES ? MN_NSTYLES - 1 : s;
}

static int lane_char(char c) { return c == 'X' ? 3 : c == 'x' ? 2 : c == 'g' ? 1 : c == 'r' ? 4 : 0; }

int mn_groove_hit(const mn_engine *e, int lane, int step)
{
    const style_t *st = &STYLES[grv_style(e)];
    const char *l = lane == 0 ? st->k : lane == 1 ? st->s : lane == 2 ? st->h : lane == 3 ? st->o : st->p;
    int v = lane_char(l[step & 31]);
    return v == 4 ? 2 : v;
}

int mn_groove_step(const mn_engine *e) { return e->running && e->gv && e->gv->on ? e->g_step : -1; }

/* ---- ein Step: in die Warteschlange mit seiner Stelle im Block ---- */
static void groove_step(mn_engine *e, int off)
{
    e->g_step = (e->g_step + 1) & 31;
    if (e->gqn < 16) {
        e->gq[e->gqn].off = (int16_t)off;
        e->gq[e->gqn].step = (uint8_t)e->g_step;
        e->gqn++;
    }
}

static float grv_rnd(mn_drums *d)        /* 0..1 */
{
    d->rs ^= d->rs << 13;
    d->rs ^= d->rs >> 17;
    d->rs ^= d->rs << 5;
    return (float)(d->rs >> 8) * (1.0f / 16777216.0f);
}

/* die Stimmen anstoßen, die dieser Step spielt */
static void groove_fire(mn_engine *e, int step)
{
    const mn_groove *g = e->gv;
    const style_t *st = &STYLES[grv_style(e)];
    mn_drums *d = &e->dr;
    float var = (float)g->var / 127.0f, km = (float)g->kick / 127.0f;
    int k = lane_char(st->k[step]), s = lane_char(st->s[step]), h = lane_char(st->h[step]);
    int o = lane_char(st->o[step]), p = lane_char(st->p[step]);
    static const float VEL[5] = {0.0f, 0.35f, 0.75f, 1.0f, 0.7f};
    /* VAR: Ghost-Hats, Wirbel, Fills am Ende des zweiten Takts, ab und zu ein Loch */
    if (var > 0.01f) {
        float r = grv_rnd(d);
        if (!h && r < var * 0.35f)
            h = 1;
        else if (h && r > 1.0f - var * 0.15f)
            h = 4;
        if (step >= 28 && !s && grv_rnd(d) < var * 0.8f)
            s = grv_rnd(d) < 0.5f ? 1 : 2;
        if (!p && grv_rnd(d) < var * 0.12f)
            p = 1;
        if (k && k < 3 && step % 4 && grv_rnd(d) < var * 0.3f)
            k = 0;
    }
    if (k) {                             /* die Kick: auch die Sidechain, ob man die Drums hört oder nicht */
        float tune = st->ktune * mn_exp2(((float)g->tune - 64.0f) / 64.0f);
        d->kph = 0.25f;                  /* (beginnt im Nulldurchgang) */
        d->kt = 0.0f;
        d->kenv = VEL[k];
        d->kfreq = tune;
        d->kdec = st->kdec * mn_exp2((km - 0.5f) * 2.0f);          /* KICK: kurz und knackig .. lang und boomig */
        d->kclick = st->kclick * (1.2f - km * 0.6f);
        d->kdrive = st->kdrive * (0.6f + km * 0.8f);
        d->kpitch = 3.2f - km * 0.8f;                                  /* der Fall der Tonhöhe, in Oktaven */
        d->sc_t = 0.0f;
        e->kick_count++;
    }
    if (s) {
        d->cenv = VEL[s];
        d->ct = 0.0f;
        d->senv = st->snare ? VEL[s] : 0.0f;
        d->sph = 0.0f;
    }
    if (h) {
        d->henv = VEL[h];
        d->oenv *= 0.1f;                 /* die geschlossene schneidet die offene ab */
        d->lvl_h = h == 4 ? -1.0f : 1.0f;   /* (-1: ein Wirbel, zwei Schläge) */
    }
    if (o)
        d->oenv = VEL[o];
    if (p) {
        d->penv = VEL[p];
        d->pph = 0.0f;
        d->pfreq = st->perc == 0 ? 1650.0f : st->perc == 2 ? 120.0f + grv_rnd(d) * 60.0f
                                                          : 300.0f * mn_exp2(grv_rnd(d) * 2.5f);
    }
}

/* nach den FX: die Pumpe auf MONSUN, dann die Drums dazu (trocken, Hats leicht verteilt) */
static void groove_render(mn_engine *e, float *l, float *r, int n)
{
    const mn_groove *g = e->gv;
    mn_drums *d = &e->dr;
    const style_t *st = &STYLES[grv_style(e)];
    float sr = e->sr, dt = 1.0f / sr;
    float pump = (float)g->pump / 127.0f, depth = 0.95f * pump * (1.3f - 0.3f * pump);   /* bis 95 % weg */
    float rel = (0.04f + 0.56f * (float)g->rel / 127.0f) * sr, atk = 0.004f * sr;
    float drums = (float)g->drums / 127.0f, hats = (float)g->hats / 127.0f;
    float dl = drums * drums * 1.1f;
    float kdec = mn_expf(-6.9f / (d->kdec * sr + 1.0f)), cdec = mn_expf(-6.9f / (0.16f * sr));
    float hdec = mn_expf(-6.9f / (0.045f * sr)), odec = mn_expf(-6.9f / (0.24f * sr));
    float pdec = mn_expf(-6.9f / ((st->perc == 2 ? 0.18f : 0.06f) * sr)), sdec = mn_expf(-6.9f / (0.09f * sr));
    float bpf = 2.0f * mn_sin(MN_PI * (st->snare ? 1900.0f : 1150.0f) / sr), bpq = st->snare ? 0.9f : 0.6f;
    int i, q = 0;
    for (i = 0; i < n; i++) {
        float kick = 0.0f, clap = 0.0f, hat = 0.0f, perc = 0.0f, nz, gain, L, R;
        while (q < e->gqn && e->gq[q].off <= i)
            groove_fire(e, e->gq[q++].step);
        /* die Sidechain: in 4 ms weg, dann mit einer runden Kurve zurück */
        d->sc_t += 1.0f;
        if (d->sc_t < atk)
            gain = 1.0f - depth * (d->sc_t / atk);
        else if (d->sc_t < atk + rel) {
            float t = 1.0f - (d->sc_t - atk) / rel;
            gain = 1.0f - depth * t * t * (3.0f - 2.0f * t) * 0.999f;   /* glatt (S-Kurve) */
        } else
            gain = 1.0f;
        if (pump < 0.005f)
            gain = 1.0f;
        e->sc_now = gain;
        /* das Rauschen für Clap, Snare, Hats */
        d->rs ^= d->rs << 13;
        d->rs ^= d->rs >> 17;
        d->rs ^= d->rs << 5;
        nz = (float)(int32_t)d->rs * (1.0f / 2147483648.0f);
        if (d->kenv > 1e-4f) {           /* die Kick: Sinus mit fallender Tonhöhe, ein Klick, etwas Drive */
            float f = d->kfreq * mn_exp2(d->kpitch * mn_expf(-d->kt * 38.0f)), y;
            d->kph += f * dt;
            if (d->kph >= 1.0f)
                d->kph -= 1.0f;
            y = mn_sin(MN_PI * (d->kph < 0.5f ? 2.0f * d->kph - 0.5f : 1.5f - 2.0f * d->kph));   /* Sinus aus der Phase */
            y *= d->kenv;
            if (d->kt < 0.003f)
                y += nz * d->kclick * (1.0f - d->kt / 0.003f) * 0.6f;
            kick = mn_soft(y * (1.0f + d->kdrive * 2.5f)) * 0.95f;
            d->kenv *= kdec;
            d->kt += dt;
        }
        if (d->cenv > 1e-4f) {           /* Clap: drei Stöße, dann der Schweif; Snare: Ton + Rauschen */
            float hp = nz - d->cbp1 - bpq * d->cbp2, env = d->cenv;
            d->cbp2 += bpf * hp;
            d->cbp1 += bpf * d->cbp2;
            if (!st->snare) {
                float ph = d->ct * 100.0f;   /* alle 10 ms ein Stoß */
                if (d->ct < 0.03f)
                    env *= 1.0f - (ph - (float)(int)ph) * 0.8f;
            }
            clap = d->cbp2 * env * (st->snare ? 1.4f : 1.8f);
            if (st->snare && d->senv > 1e-4f) {
                float f = 185.0f * (1.0f + 0.5f * mn_expf(-d->ct * 60.0f));
                d->sph += f * dt;
                if (d->sph >= 1.0f)
                    d->sph -= 1.0f;
                clap += mn_sin(MN_PI * (d->sph < 0.5f ? 2.0f * d->sph - 0.5f : 1.5f - 2.0f * d->sph)) * d->senv * 0.55f;
                d->senv *= sdec;
            }
            d->cenv *= cdec;
            d->ct += dt;
        }
        {   /* Hats: Rauschen durch zwei Hochpässe */
            float a = 0.55f, h1, h2;
            h1 = nz - d->hlp1;
            d->hlp1 += a * h1 * 0.25f;
            h2 = h1 - d->hlp2;
            d->hlp2 += a * h2 * 0.35f;
            if (d->lvl_h < 0.0f && d->henv < 0.25f) {   /* ein Wirbel: der zweite Schlag nach der halben Zeit */
                d->henv = 0.6f;
                d->lvl_h = 1.0f;
            }
            hat = h2 * (d->henv + d->oenv * 0.8f) * 0.55f * hats;
            d->henv *= hdec;
            d->oenv *= odec;
        }
        if (d->penv > 1e-4f) {           /* Perc: ein kurzer Ton */
            d->pph += d->pfreq * dt;
            if (d->pph >= 1.0f)
                d->pph -= 1.0f;
            perc = mn_sin(MN_PI * (d->pph < 0.5f ? 2.0f * d->pph - 0.5f : 1.5f - 2.0f * d->pph)) * d->penv * 0.45f;
            d->penv *= pdec;
        }
        L = l[i] * gain + (kick + clap + hat * 0.8f + perc * 1.1f) * dl;
        R = r[i] * gain + (kick + clap + hat * 1.2f + perc * 0.9f) * dl;
        l[i] = mn_soft(L);
        r[i] = mn_soft(R);
    }
    while (q < e->gqn)                   /* (kommt nicht vor: alle Stellen liegen im Block) */
        groove_fire(e, e->gq[q++].step);
    e->gqn = 0;
}
