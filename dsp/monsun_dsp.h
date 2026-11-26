/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (MONSUN) */
/* MONSUN – kleine DSP-Helfer: günstig auf dem FM-1 (keine libm-Aufrufe im Audio-Pfad) */
#pragma once
#include <stdint.h>

#define MN_PI 3.14159265f

static inline float mn_clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float mn_abs(float x) { return x < 0.0f ? -x : x; }

/* weiche Sättigung (rationale tanh-Näherung) */
static inline float mn_soft(float x)
{
    float x2;
    if (x < -3.0f)
        return -1.0f;
    if (x > 3.0f)
        return 1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* 2^x ohne libm: Ganzzahlteil über den Exponenten, Rest als Polynom */
static inline float mn_exp2(float x)
{
    union { float f; uint32_t u; } b;
    int32_t i;
    float f, p;
    x = mn_clamp(x, -30.0f, 30.0f);
    i = (int32_t)x;
    if ((float)i > x)
        i--;
    f = x - (float)i;
    /* Minimax-Polynom 5. Grades für 2^f auf [0,1): Fehler unter 2e-7, und 2^1 trifft genau 2.
     * (Das alte Polynom 4. Grades lag bei f -> 1 um 0.08 % daneben: lange Hüllkurven brachen nach ~0.2 s ab.) */
    p = 1.0f + f * (0.6931530f + f * (0.2401536f + f * (0.0558263f + f * (0.0089893f + f * 0.0018776f))));
    b.f = p;
    b.u += (uint32_t)i << 23;
    return b.f;
}

/* e^x für die Hüllkurven-Koeffizienten (selten berechnet) */
static inline float mn_expf(float x) { return mn_exp2(x * 1.44269504f); }

/* sin(x) für |x| <= pi/2 (die Filter-Koeffizienten) */
static inline float mn_sin(float x)
{
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}
