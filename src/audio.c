/*
 * Audio: mixes positional sound effects, runs a small organ/bell music
 * sequencer and a Schroeder reverb for a cathedral acoustic, and feeds the
 * result to an SDL3 audio stream. If no audio device can be opened the game
 * simply runs silent.
 */
#include "common.h"

#include <SDL3/SDL.h>

/* keep about this much audio queued ahead of the device */
#define QUEUE_MS 50

static SDL_AudioStream *s_stream;
static bool s_ok;

static Sample s_snd[SND_COUNT];
static float s_lx, s_ly, s_lang;

/* ------------------------------------------------------------------ */
/* mixer                                                               */
/* ------------------------------------------------------------------ */
#define NVOICES 32
typedef struct { const Sample *s; float pos, step, vl, vr; int id; u32 age; } Voice;
static Voice s_voices[NVOICES];
static u32 s_age;

void snd_play(int id, float vol, float pan, float pitch)
{
    if (!s_ok || id < 0 || id >= SND_COUNT || !s_snd[id].data) return;
    int best = 0;
    u32 oldest = 0xffffffffu;
    for (int i = 0; i < NVOICES; i++) {
        if (!s_voices[i].s) { best = i; break; }
        if (s_voices[i].age < oldest) { oldest = s_voices[i].age; best = i; }
    }
    pan = clampf(pan, -1, 1);
    float a = (pan + 1) * PI * 0.25f;
    Voice *v = &s_voices[best];
    v->s = &s_snd[id];
    v->pos = 0;
    v->step = pitch;
    v->vl = vol * cosf(a) * 1.414f * 0.5f;
    v->vr = vol * sinf(a) * 1.414f * 0.5f;
    v->id = id;
    v->age = ++s_age;
}

void snd_listener(float x, float y, float ang) { s_lx = x; s_ly = y; s_lang = ang; }

void snd_play_at(int id, float x, float y, float vol)
{
    float dx = x - s_lx, dy = y - s_ly, d = hypotf(dx, dy);
    float att = vol / (1.0f + d * 0.16f + d * d * 0.012f);
    if (att < 0.02f) return;
    float pan = d > 0.01f ? sinf(atan2f(dy, dx) - s_lang) * minf(1, d * 0.5f) : 0;
    snd_play(id, att, pan * 0.85f, frand2(0.94f, 1.06f));
}

/* ------------------------------------------------------------------ */
/* music sequencer                                                     */
/* ------------------------------------------------------------------ */
#define TABLE 4096
static float s_sine[TABLE], s_organ[TABLE];

typedef struct {
    float bpm;
    int nchords;
    int chords[8][2];  /* root midi note, minor(1)/major(0) */
    float pad, bass, arp;
    int bass_mode;     /* 0 sustained, 1 pulsing eighths */
    int arp_mode;      /* 0 none, 1 toccata sixteenths, 2 sparse bells */
    bool drums;
} Song;

static const Song SONGS[SONG_COUNT] = {
    [SONG_TITLE] = {48, 4, {{50, 1}, {46, 0}, {43, 1}, {45, 0}}, 0.10f, 0.10f, 0.06f, 0, 2, false},
    [SONG_GRAVEYARD] = {54, 4, {{45, 1}, {41, 0}, {50, 1}, {52, 0}}, 0.07f, 0.11f, 0.07f, 0, 2, false},
    [SONG_CATHEDRAL] = {66, 8, {{50, 1}, {43, 1}, {45, 0}, {50, 1}, {46, 0}, {43, 1}, {45, 0}, {45, 0}}, 0.08f, 0.10f, 0.045f, 0, 1, false},
    [SONG_OSSUARY] = {88, 4, {{48, 1}, {44, 0}, {41, 1}, {43, 0}}, 0.07f, 0.12f, 0.05f, 1, 2, true},
    [SONG_VICTORY] = {60, 4, {{50, 0}, {43, 0}, {45, 0}, {50, 0}}, 0.10f, 0.09f, 0.07f, 0, 2, false},
};

enum { MW_ORGAN, MW_BELL, MW_DRUM };
typedef struct { float phase, inc, amp, env, rel; int wave; bool on; float t; } MVoice;
#define NMV 16
static MVoice s_mv[NMV];
static int s_song = -1;
static bool s_music_on = true;
static double s_step_acc;
static int s_step;
static float s_music_gain, s_music_target;
static u32 s_mrng = 12345;

static float mrand(void) { s_mrng = s_mrng * 1664525u + 1013904223u; return (s_mrng >> 8) / 16777216.0f; }
static float midi_hz(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

static void mv_note(int note, float amp, int wave, float dur)
{
    int best = 0;
    float quiet = 1e9f;
    for (int i = 0; i < NMV; i++) {
        if (!s_mv[i].on) { best = i; quiet = -1; break; }
        if (s_mv[i].env * s_mv[i].amp < quiet) { quiet = s_mv[i].env * s_mv[i].amp; best = i; }
    }
    MVoice *v = &s_mv[best];
    v->on = true;
    v->phase = 0;
    v->inc = midi_hz(note) * TABLE / AUDIO_RATE;
    v->amp = amp;
    v->env = wave == MW_ORGAN ? 0 : 1;
    v->wave = wave;
    v->rel = dur;
    v->t = 0;
}

static void music_step(void)
{
    const Song *s = &SONGS[s_song];
    int bar = s_step / 16, st = s_step % 16;
    const int *ch = s->chords[bar % s->nchords];
    int root = ch[0], third = ch[1] ? 3 : 4;
    float bar_len = 60.0f / s->bpm * 4;
    int tones[6] = {root + 12, root + 12 + third, root + 19, root + 24, root + 24 + third, root + 31};
    if (st == 0) {
        mv_note(root + 12, s->pad, MW_ORGAN, bar_len);
        mv_note(root + 12 + third, s->pad * 0.8f, MW_ORGAN, bar_len);
        mv_note(root + 19, s->pad * 0.8f, MW_ORGAN, bar_len);
        if (s->bass_mode == 0) mv_note(root - 12, s->bass, MW_ORGAN, bar_len);
    }
    if (s->bass_mode == 1 && st % 2 == 0) mv_note(root - 12 + (st % 8 == 6 ? 7 : 0), s->bass, MW_ORGAN, bar_len / 10);
    if (s->arp_mode == 1) {
        static const int pat[16] = {0, 1, 2, 3, 4, 5, 4, 3, 2, 3, 4, 5, 4, 3, 2, 1};
        mv_note(tones[pat[st]], s->arp, MW_ORGAN, bar_len / 18);
    } else if (s->arp_mode == 2 && st % 4 == 0 && mrand() < 0.6f) {
        mv_note(tones[(int)(mrand() * 6)] + 12, s->arp, MW_BELL, 0);
    }
    if (s->drums && (st == 0 || st == 8 || (st == 14 && mrand() < 0.5f))) mv_note(root - 24, 0.35f, MW_DRUM, 0);
    s_step++;
}

static float music_sample(void)
{
    if (s_song < 0) return 0;
    const Song *s = &SONGS[s_song];
    s_step_acc += 1.0;
    double step_len = AUDIO_RATE * 60.0 / s->bpm / 4.0;
    if (s_step_acc >= step_len) { s_step_acc -= step_len; music_step(); }
    float out = 0;
    const float dt = 1.0f / AUDIO_RATE;
    for (int i = 0; i < NMV; i++) {
        MVoice *v = &s_mv[i];
        if (!v->on) continue;
        v->t += dt;
        float smp;
        int idx = (int)v->phase & (TABLE - 1);
        switch (v->wave) {
        case MW_ORGAN:
            if (v->t < v->rel) v->env = minf(1, v->env + dt * 14);
            else v->env -= dt * 3.5f;
            smp = s_organ[idx] * (1.0f + 0.04f * s_sine[(int)(v->t * 5.5f * TABLE) & (TABLE - 1)]);
            break;
        case MW_BELL:
            v->env *= 1.0f - dt * 1.6f;
            smp = s_sine[idx] + 0.5f * s_sine[(int)(v->phase * 2.76f) & (TABLE - 1)] * v->env +
                  0.25f * s_sine[(int)(v->phase * 5.4f) & (TABLE - 1)] * v->env * v->env;
            break;
        default:
            v->env *= 1.0f - dt * 5.0f;
            smp = s_sine[idx] * (1.0f + v->env) + (mrand() - 0.5f) * v->env * v->env * 0.6f;
            v->phase -= v->inc * 0.3f * (1 - v->env);
            break;
        }
        v->phase += v->inc;
        if (v->phase >= TABLE * 64) v->phase -= TABLE * 64;
        if (v->env <= 0.0005f && v->t > 0.05f) { v->on = false; continue; }
        out += smp * v->env * v->amp;
    }
    return out;
}

void music_play(int song)
{
    if (song == s_song) return;
    s_song = song;
    s_step = 0;
    s_step_acc = 1e9;
    for (int i = 0; i < NMV; i++) s_mv[i].rel = 0;
}

void music_set_enabled(bool on) { s_music_on = on; }
bool music_enabled(void) { return s_music_on; }

/* ------------------------------------------------------------------ */
/* reverb (Freeverb-style combs + allpasses)                           */
/* ------------------------------------------------------------------ */
typedef struct { float *buf; int len, pos; float fb, damp, store; } Comb;
typedef struct { float *buf; int len, pos; } Allpass;
static Comb s_comb[2][4];
static Allpass s_ap[2][2];

static void reverb_init(void)
{
    static const int cl[4] = {1557, 1617, 1491, 1422}, al[2] = {556, 441};
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 4; i++) {
            Comb *k = &s_comb[c][i];
            k->len = (cl[i] + c * 23) * AUDIO_RATE / 44100;
            k->buf = calloc((size_t)k->len, sizeof(float));
            k->fb = 0.83f;
            k->damp = 0.3f;
        }
        for (int i = 0; i < 2; i++) {
            Allpass *a = &s_ap[c][i];
            a->len = (al[i] + c * 23) * AUDIO_RATE / 44100;
            a->buf = calloc((size_t)a->len, sizeof(float));
        }
    }
}

static float reverb(int c, float in)
{
    float out = 0;
    for (int i = 0; i < 4; i++) {
        Comb *k = &s_comb[c][i];
        float y = k->buf[k->pos];
        k->store = y * (1 - k->damp) + k->store * k->damp;
        k->buf[k->pos] = in + k->store * k->fb;
        if (++k->pos >= k->len) k->pos = 0;
        out += y;
    }
    for (int i = 0; i < 2; i++) {
        Allpass *a = &s_ap[c][i];
        float b = a->buf[a->pos];
        a->buf[a->pos] = out + b * 0.5f;
        if (++a->pos >= a->len) a->pos = 0;
        out = b - out;
    }
    return out;
}

static void mix(i16 *out, int frames)
{
    for (int f = 0; f < frames; f++) {
        float l = 0, r = 0;
        for (int i = 0; i < NVOICES; i++) {
            Voice *v = &s_voices[i];
            if (!v->s) continue;
            int p = (int)v->pos;
            if (p >= v->s->len - 1) { v->s = NULL; continue; }
            float fr = v->pos - p;
            float smp = v->s->data[p] + (v->s->data[p + 1] - v->s->data[p]) * fr;
            l += smp * v->vl;
            r += smp * v->vr;
            v->pos += v->step;
        }
        s_music_gain += (s_music_target - s_music_gain) * 0.0005f;
        float m = music_sample() * s_music_gain;
        float send = (l + r) * 0.18f + m * 0.6f;
        float wl = reverb(0, send * 0.15f), wr = reverb(1, send * 0.15f);
        l += m + wl;
        r += m + wr;
        l = l / (1.0f + fabsf(l) * 0.3f);
        r = r / (1.0f + fabsf(r) * 0.3f);
        out[f * 2] = (i16)clampi((int)(l * 30000), -32767, 32767);
        out[f * 2 + 1] = (i16)clampi((int)(r * 30000), -32767, 32767);
    }
}

/* ------------------------------------------------------------------ */
/* pump                                                                */
/* ------------------------------------------------------------------ */
void audio_update(void)
{
    if (!s_ok) return;
    s_music_target = s_music_on ? 1.0f : 0.0f;
    static i16 tmp[4096];
    const int frame_bytes = 4, target = AUDIO_RATE * frame_bytes * QUEUE_MS / 1000;
    int queued = SDL_GetAudioStreamQueued(s_stream);
    if (queued < 0) return;
    while (queued < target) {
        int frames = (target - queued) / frame_bytes;
        if (frames > (int)(sizeof tmp / frame_bytes)) frames = (int)(sizeof tmp / frame_bytes);
        if (frames < 1) break;
        mix(tmp, frames);
        if (!SDL_PutAudioStreamData(s_stream, tmp, frames * frame_bytes)) break;
        queued += frames * frame_bytes;
    }
}

bool audio_init(void)
{
    for (int i = 0; i < TABLE; i++) {
        float ph = (float)i / TABLE * TAU;
        s_sine[i] = sinf(ph);
        s_organ[i] = (sinf(ph) + 0.5f * sinf(2 * ph) + 0.33f * sinf(3 * ph) + 0.25f * sinf(4 * ph) +
                      0.12f * sinf(6 * ph) + 0.1f * sinf(8 * ph)) / 2.3f;
    }
    if (getenv("NOCTIS_NO_AUDIO")) return false;
    const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AUDIO_RATE};
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO) ||
        !(s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL))) {
        fprintf(stderr, "audio: %s, running silent\n", SDL_GetError());
        return false;
    }
    reverb_init();
    sfx_generate(s_snd);
    SDL_ResumeAudioStreamDevice(s_stream);
    s_ok = true;
    return true;
}

void audio_shutdown(void)
{
    if (s_stream) SDL_DestroyAudioStream(s_stream);
    s_stream = NULL;
    s_ok = false;
}
