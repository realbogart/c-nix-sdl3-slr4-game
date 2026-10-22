/*
 * Procedural sound effects: every sample in the game is synthesised at
 * startup from oscillators, noise, filters and envelopes.
 */
#include "common.h"

#define SR ((float)AUDIO_RATE)

static u32 s_seed = 0xC0FFEEu;
static float nz(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return (s_seed & 0xffffff) / 8388608.0f - 1.0f;
}

typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } Biquad;

static void bq_set(Biquad *f, int type, float freq, float q)
{
    freq = clampf(freq, 10, SR * 0.45f);
    float w = TAU * freq / SR, cs = cosf(w), al = sinf(w) / (2 * q);
    float b0, b1, b2, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
    if (type == 0) { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; }       /* lowpass */
    else if (type == 1) { b0 = al; b1 = 0; b2 = -al; }                         /* bandpass */
    else { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; }             /* highpass */
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0; f->a1 = a1 / a0; f->a2 = a2 / a0;
}

static float bq(Biquad *f, float x)
{
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x;
    f->y2 = f->y1; f->y1 = y;
    return y;
}

static Sample alloc_s(float secs)
{
    Sample s;
    s.len = (int)(secs * SR);
    s.data = calloc((size_t)s.len + 1, sizeof(float));
    return s;
}

static void normalize(Sample *s, float peak)
{
    float m = 1e-6f;
    for (int i = 0; i < s->len; i++) m = maxf(m, fabsf(s->data[i]));
    float k = peak / m;
    for (int i = 0; i < s->len; i++) s->data[i] *= k;
    /* short fade-out to avoid clicks */
    int fade = (int)(SR * 0.01f);
    for (int i = 0; i < fade && i < s->len; i++) s->data[s->len - 1 - i] *= (float)i / fade;
}

static float saw(float ph) { return 2.0f * (ph - floorf(ph)) - 1.0f; }

/* voiced growl through two formants */
static void growl(Sample *s, float f0, float f1, float fa, float fb, float noise, float dist, float vib)
{
    Biquad A = {0}, B = {0}, N = {0};
    float ph = 0;
    for (int i = 0; i < s->len; i++) {
        float t = (float)i / s->len;
        float f = lerpf(f0, f1, t) * (1.0f + vib * sinf(i / SR * TAU * 7.0f) + 0.03f * nz());
        ph += f / SR;
        bq_set(&A, 1, fa * (0.9f + 0.2f * t), 4);
        bq_set(&B, 1, fb, 5);
        bq_set(&N, 1, fa * 1.5f, 1.5f);
        float src = saw(ph) + noise * nz();
        float y = bq(&A, src) * 1.2f + bq(&B, src) * 0.8f + bq(&N, nz()) * noise;
        float env = sinf(minf(t * 6, 1.0f) * PI * 0.5f) * (1 - t) * (1 - t * 0.3f);
        s->data[i] = tanhf(y * dist) * env;
    }
}

static void clicks(Sample *s, int n, float lo, float hi, float decay)
{
    Biquad f = {0};
    for (int k = 0; k < n; k++) {
        float at = powf((float)k / n, decay) * 0.9f + nz() * 0.02f;
        int start = clampi((int)(at * s->len), 0, s->len - 1);
        bq_set(&f, 1, lerpf(lo, hi, nz() * 0.5f + 0.5f), 6);
        int len = (int)(SR * 0.012f);
        for (int i = 0; i < len && start + i < s->len; i++)
            s->data[start + i] += bq(&f, nz() * expf(-i / (SR * 0.002f))) * 3.0f;
    }
}

static void tone(Sample *s, float at, float freq, float amp, float decay, const float *partials, int np)
{
    int start = (int)(at * SR);
    for (int i = start; i < s->len; i++) {
        float t = (i - start) / SR;
        float v = 0;
        for (int p = 0; p < np; p++) v += sinf(TAU * freq * partials[p * 2] * t) * partials[p * 2 + 1] * expf(-t * decay * (1 + p * 0.6f));
        s->data[i] += v * amp;
    }
}

static void boom(Sample *s, float f0, float decay, float cut0, float cut1, float thump)
{
    Biquad lp = {0};
    float ph = 0;
    for (int i = 0; i < s->len; i++) {
        float t = i / SR;
        bq_set(&lp, 0, lerpf(cut0, cut1, minf(1, t * 3)), 0.8f);
        float n = bq(&lp, nz()) * expf(-t * decay);
        ph += f0 * (1.0f + expf(-t * 20)) / SR;
        float th = sinf(TAU * ph) * expf(-t * decay * 1.3f) * thump;
        s->data[i] += tanhf((n * 2.5f + th) * 1.5f);
    }
}

void sfx_generate(Sample *out)
{
    Biquad f = {0}, g = {0};
    Sample s;

    /* dagger swish */
    s = alloc_s(0.28f);
    for (int i = 0; i < s.len; i++) {
        float t = (float)i / s.len;
        bq_set(&f, 1, 700 + 2600 * t, 2);
        s.data[i] = bq(&f, nz()) * sinf(t * PI) * sinf(t * PI);
    }
    normalize(&s, 0.6f);
    out[SND_DAGGER] = s;

    /* dagger hit */
    s = alloc_s(0.2f);
    boom(&s, 110, 18, 1800, 400, 1.2f);
    normalize(&s, 0.8f);
    out[SND_DAGGER_HIT] = s;

    /* crossbow: plucked string + whoosh */
    s = alloc_s(0.5f);
    {
        int period = (int)(SR / 170);
        float *ks = calloc((size_t)period, sizeof(float));
        for (int i = 0; i < period; i++) ks[i] = nz();
        int p = 0;
        for (int i = 0; i < s.len; i++) {
            float a = ks[p], b = ks[(p + 1) % period];
            ks[p] = (a + b) * 0.5f * 0.993f;
            p = (p + 1) % period;
            float t = i / SR;
            s.data[i] = a * 0.9f;
            bq_set(&f, 1, 1400 + 1500 * t, 1.5f);
            s.data[i] += bq(&f, nz()) * expf(-t * 14) * 0.8f;
            if (i < SR * 0.006f) s.data[i] += nz() * 1.2f;
        }
        free(ks);
    }
    normalize(&s, 0.8f);
    out[SND_CROSSBOW] = s;

    /* blunderbuss */
    s = alloc_s(1.3f);
    boom(&s, 52, 4.5f, 6000, 250, 1.6f);
    for (int k = 0; k < 40; k++) {
        int at = (int)((0.08f + powf(nz() * 0.5f + 0.5f, 2) * 0.9f) * s.len);
        for (int i = 0; i < 60 && at + i < s.len; i++) s.data[at + i] += nz() * 0.25f * expf(-i / 12.0f);
    }
    normalize(&s, 0.95f);
    out[SND_BLUNDER] = s;

    /* dry fire click */
    s = alloc_s(0.08f);
    bq_set(&f, 2, 2500, 0.7f);
    for (int i = 0; i < s.len; i++) s.data[i] = bq(&f, nz()) * expf(-i / (SR * 0.006f));
    normalize(&s, 0.5f);
    out[SND_DRYFIRE] = s;

    static const float bellp[] = {1, 1, 2.0f, 0.4f, 3.01f, 0.2f};
    s = alloc_s(0.6f);
    tone(&s, 0, 659.25f, 0.5f, 7, bellp, 3);
    tone(&s, 0.08f, 987.77f, 0.5f, 6, bellp, 3);
    normalize(&s, 0.6f);
    out[SND_PICKUP] = s;

    static const float metal[] = {1, 1, 2.7f, 0.5f, 4.1f, 0.3f};
    s = alloc_s(0.3f);
    tone(&s, 0, 2200, 0.4f, 30, metal, 3);
    tone(&s, 0.05f, 3100, 0.3f, 35, metal, 3);
    tone(&s, 0.11f, 2650, 0.3f, 35, metal, 3);
    normalize(&s, 0.5f);
    out[SND_AMMO] = s;

    s = alloc_s(1.0f);
    tone(&s, 0.00f, 880.0f, 0.5f, 4, bellp, 3);
    tone(&s, 0.07f, 1108.7f, 0.5f, 4, bellp, 3);
    tone(&s, 0.14f, 1318.5f, 0.5f, 4, bellp, 3);
    tone(&s, 0.21f, 1760.0f, 0.5f, 3, bellp, 3);
    normalize(&s, 0.6f);
    out[SND_KEY] = s;

    static const float organp[] = {1, 1, 2, 0.5f, 3, 0.3f, 4, 0.2f};
    s = alloc_s(1.2f);
    tone(&s, 0, 293.66f, 0.4f, 2.5f, organp, 4);
    tone(&s, 0, 369.99f, 0.4f, 2.5f, organp, 4);
    tone(&s, 0, 440.0f, 0.4f, 2.5f, organp, 4);
    tone(&s, 0.1f, 1174.7f, 0.3f, 4, bellp, 3);
    normalize(&s, 0.6f);
    out[SND_WEAPON] = s;

    /* door open: rumble + creak */
    s = alloc_s(1.1f);
    {
        float ph = 0;
        bq_set(&g, 0, 160, 0.7f);
        for (int i = 0; i < s.len; i++) {
            float t = (float)i / s.len;
            float rumble = bq(&g, nz()) * 3.0f * sinf(t * PI);
            float fq = 330 + 140 * sinf(t * 9) + 60 * nz();
            ph += fq / SR;
            float creak = sinf(TAU * ph) * (0.5f + 0.5f * sinf(t * 140)) * (t < 0.65f ? sinf(t / 0.65f * PI) : 0);
            s.data[i] = rumble + creak * 0.35f;
        }
    }
    normalize(&s, 0.6f);
    out[SND_DOOR_OPEN] = s;

    s = alloc_s(0.5f);
    boom(&s, 70, 9, 400, 150, 1.4f);
    normalize(&s, 0.7f);
    out[SND_DOOR_CLOSE] = s;

    s = alloc_s(0.35f);
    tone(&s, 0, 180, 0.6f, 18, metal, 3);
    tone(&s, 0.09f, 150, 0.6f, 18, metal, 3);
    for (int i = 0; i < SR * 0.01f; i++) s.data[i] += nz() * 0.5f;
    normalize(&s, 0.6f);
    out[SND_LOCKED] = s;

    s = alloc_s(0.9f); growl(&s, 85, 65, 520, 1150, 0.5f, 2.5f, 0.06f); normalize(&s, 0.8f); out[SND_GHOUL_SIGHT] = s;
    s = alloc_s(0.4f); growl(&s, 120, 95, 650, 1300, 0.6f, 3.0f, 0.05f); normalize(&s, 0.75f); out[SND_GHOUL_ATTACK] = s;
    s = alloc_s(0.3f); growl(&s, 170, 110, 750, 1400, 0.3f, 2.0f, 0.03f); normalize(&s, 0.7f); out[SND_GHOUL_PAIN] = s;
    s = alloc_s(1.2f);
    growl(&s, 120, 45, 480, 950, 0.7f, 2.5f, 0.1f);
    for (int i = 0; i < s.len; i++) s.data[i] *= 0.6f + 0.4f * sinf(i / SR * TAU * 11);
    normalize(&s, 0.8f);
    out[SND_GHOUL_DIE] = s;

    s = alloc_s(0.6f);
    clicks(&s, 16, 1400, 3200, 1.0f);
    normalize(&s, 0.6f);
    out[SND_SKEL_SIGHT] = s;

    s = alloc_s(1.0f);
    clicks(&s, 30, 900, 3000, 1.8f);
    boom(&(Sample){s.data, s.len / 6}, 90, 12, 600, 200, 0.4f);
    normalize(&s, 0.7f);
    out[SND_SKEL_DIE] = s;

    /* wraith wail */
    s = alloc_s(1.5f);
    {
        float ph = 0;
        bq_set(&g, 1, 420, 2);
        for (int i = 0; i < s.len; i++) {
            float t = (float)i / s.len;
            float fq = (t < 0.4f ? lerpf(380, 640, t / 0.4f) : lerpf(640, 290, (t - 0.4f) / 0.6f)) * (1 + 0.025f * sinf(i / SR * TAU * 6));
            ph += fq / SR;
            float v = sinf(TAU * ph) + 0.3f * sinf(TAU * ph * 2) + bq(&g, nz()) * 1.5f;
            s.data[i] = v * sinf(t * PI) * (1 - t * 0.5f);
        }
    }
    normalize(&s, 0.6f);
    out[SND_WRAITH_SIGHT] = s;

    s = alloc_s(1.6f);
    {
        float ph = 0;
        bq_set(&g, 1, 600, 1.5f);
        for (int i = 0; i < s.len; i++) {
            float t = (float)i / s.len;
            float fq = lerpf(620, 140, t) * (1 + 0.04f * sinf(i / SR * TAU * 9));
            ph += fq / SR;
            s.data[i] = (sinf(TAU * ph) * (1 - t) + bq(&g, nz()) * t * 2) * (1 - t) * minf(1, t * 20);
        }
    }
    normalize(&s, 0.6f);
    out[SND_WRAITH_DIE] = s;

    /* bat chirps */
    s = alloc_s(0.25f);
    for (int k = 0; k < 3; k++) {
        float ph = 0;
        int st = (int)(k * 0.07f * SR), len = (int)(0.04f * SR);
        for (int i = 0; i < len && st + i < s.len; i++) {
            float t = (float)i / len;
            ph += lerpf(4200, 2600, t) / SR;
            s.data[st + i] += sinf(TAU * ph) * sinf(t * PI);
        }
    }
    normalize(&s, 0.4f);
    out[SND_BAT] = s;

    s = alloc_s(0.35f);
    {
        float ph = 0;
        for (int i = 0; i < s.len; i++) {
            float t = (float)i / s.len;
            ph += lerpf(3000, 900, t) / SR;
            s.data[i] = sinf(TAU * ph) * (1 - t) + nz() * 0.1f * (1 - t);
        }
    }
    normalize(&s, 0.45f);
    out[SND_BAT_DIE] = s;

    s = alloc_s(2.0f); growl(&s, 58, 44, 320, 720, 0.8f, 4.0f, 0.08f); normalize(&s, 0.95f); out[SND_DEMON_SIGHT] = s;
    s = alloc_s(0.6f); growl(&s, 80, 60, 380, 820, 0.6f, 3.5f, 0.05f); normalize(&s, 0.9f); out[SND_DEMON_PAIN] = s;
    s = alloc_s(3.2f);
    growl(&s, 70, 25, 300, 650, 0.9f, 4.0f, 0.12f);
    normalize(&s, 0.95f);
    out[SND_DEMON_DIE] = s;

    /* fireball whoosh */
    s = alloc_s(0.6f);
    for (int i = 0; i < s.len; i++) {
        float t = (float)i / s.len;
        bq_set(&f, 1, 400 + 1400 * t, 1.4f);
        s.data[i] = bq(&f, nz()) * sinf(minf(1, t * 4) * PI * 0.5f) * (1 - t) + (nz() > 0.995f ? nz() * 0.3f : 0);
    }
    normalize(&s, 0.6f);
    out[SND_FIREBALL] = s;

    s = alloc_s(0.9f); boom(&s, 60, 6, 3000, 200, 1.0f); normalize(&s, 0.85f); out[SND_EXPLODE] = s;
    s = alloc_s(0.16f); boom(&s, 95, 25, 900, 300, 0.8f); normalize(&s, 0.6f); out[SND_FLESH] = s;
    s = alloc_s(0.1f); boom(&s, 200, 40, 3000, 900, 0.3f); normalize(&s, 0.4f); out[SND_WALLHIT] = s;

    s = alloc_s(0.35f); growl(&s, 140, 105, 720, 1150, 0.2f, 1.6f, 0.02f); normalize(&s, 0.7f); out[SND_PLAYER_HURT] = s;
    s = alloc_s(1.4f); growl(&s, 160, 70, 700, 1100, 0.3f, 1.8f, 0.05f); normalize(&s, 0.8f); out[SND_PLAYER_DIE] = s;

    /* thunder: crack + brown-noise rumble */
    s = alloc_s(4.5f);
    {
        float br = 0;
        bq_set(&f, 0, 320, 0.7f);
        bq_set(&g, 2, 1500, 0.7f);
        float amp = 1;
        for (int i = 0; i < s.len; i++) {
            float t = i / SR;
            br = br * 0.995f + nz() * 0.05f;
            if ((i % 2400) == 0) amp = 0.4f + (nz() * 0.5f + 0.5f) * 0.6f;
            float rumble = bq(&f, br) * 6.0f * amp * expf(-t * 0.7f) * minf(1, t * 4);
            float crack = t < 0.25f ? bq(&g, nz()) * expf(-t * 14) * 1.5f : 0;
            s.data[i] = tanhf(rumble + crack);
        }
    }
    normalize(&s, 0.9f);
    out[SND_THUNDER] = s;

    /* church bell */
    s = alloc_s(5.0f);
    {
        static const float ratio[] = {0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f};
        static const float amp[] = {0.5f, 1.0f, 0.6f, 0.4f, 0.6f, 0.3f, 0.25f, 0.2f};
        static const float dec[] = {0.35f, 0.6f, 0.8f, 1.0f, 1.2f, 1.6f, 1.8f, 2.2f};
        for (int i = 0; i < s.len; i++) {
            float t = i / SR, v = 0;
            for (int p = 0; p < 8; p++) v += sinf(TAU * 196.0f * ratio[p] * t) * amp[p] * expf(-t * dec[p]);
            s.data[i] = v * minf(1, t * 400);
        }
    }
    normalize(&s, 0.7f);
    out[SND_BELL] = s;

    /* exit: choir chord */
    s = alloc_s(2.8f);
    {
        static const float notes[] = {146.83f, 220.0f, 293.66f, 369.99f, 440.0f};
        Biquad fa[5] = {{0}}, fb[5] = {{0}};
        float ph[5] = {0};
        for (int v = 0; v < 5; v++) { bq_set(&fa[v], 1, 800, 5); bq_set(&fb[v], 1, 1150, 6); }
        for (int i = 0; i < s.len; i++) {
            float t = (float)i / s.len, acc = 0;
            for (int v = 0; v < 5; v++) {
                ph[v] += notes[v] * (1 + 0.006f * sinf(i / SR * TAU * (5 + v * 0.3f))) / SR;
                float src = saw(ph[v]);
                acc += bq(&fa[v], src) + bq(&fb[v], src) * 0.6f;
            }
            s.data[i] = acc * minf(1, t * 4) * (1 - t);
        }
        tone(&s, 0.2f, 1174.7f, 0.15f, 1.5f, bellp, 3);
    }
    normalize(&s, 0.7f);
    out[SND_EXIT] = s;

    s = alloc_s(0.09f);
    tone(&s, 0, 880, 0.5f, 25, bellp, 2);
    normalize(&s, 0.35f);
    out[SND_MENU] = s;
}
