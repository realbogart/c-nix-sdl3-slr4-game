/*
 * Audio: speaks the PulseAudio native protocol directly (works with
 * PulseAudio and pipewire-pulse), mixes positional sound effects, runs a
 * small organ/bell music sequencer and a Schroeder reverb for a cathedral
 * acoustic. If no server is reachable the game simply runs silent.
 */
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define PA_VERSION 13
#define CMD_ERROR 0
#define CMD_REPLY 2
#define CMD_CREATE_PLAYBACK_STREAM 3
#define CMD_AUTH 8
#define CMD_SET_CLIENT_NAME 9
#define CMD_REQUEST 61
#define CTRL_CHANNEL 0xffffffffu

static int s_fd = -1;
static bool s_ok;
static u32 s_channel;
static u32 s_requested;
static u8 s_rbuf[1 << 16];
static int s_rlen;
static u8 s_wbuf[1 << 17];
static int s_wlen;

static Sample s_snd[SND_COUNT];
static float s_lx, s_ly, s_lang;

/* ------------------------------------------------------------------ */
/* tagstruct                                                           */
/* ------------------------------------------------------------------ */
typedef struct { u8 d[2048]; int n; } TS;

static void ts_raw(TS *t, const void *p, int n) { if (t->n + n <= (int)sizeof t->d) { memcpy(t->d + t->n, p, (size_t)n); t->n += n; } }
static void ts_byte(TS *t, u8 b) { ts_raw(t, &b, 1); }
static void be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 rbe32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void ts_u32(TS *t, u32 v) { u8 b[5] = {'L'}; be32(b + 1, v); ts_raw(t, b, 5); }
static void ts_bool(TS *t, bool v) { ts_byte(t, v ? '1' : '0'); }
static void ts_str(TS *t, const char *s) { ts_byte(t, 't'); ts_raw(t, s, (int)strlen(s) + 1); }
static void ts_null(TS *t) { ts_byte(t, 'N'); }
static void ts_arb(TS *t, const void *p, u32 n) { u8 b[5] = {'x'}; be32(b + 1, n); ts_raw(t, b, 5); ts_raw(t, p, (int)n); }

static void ts_props(TS *t, const char *k, const char *v)
{
    ts_byte(t, 'P');
    ts_str(t, k);
    ts_u32(t, (u32)strlen(v) + 1);
    ts_arb(t, v, (u32)strlen(v) + 1);
    ts_null(t);
}

static bool send_all(const void *p, int n)
{
    const u8 *b = p;
    while (n > 0) {
        ssize_t w = write(s_fd, b, (size_t)n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { struct pollfd pf = {s_fd, POLLOUT, 0}; poll(&pf, 1, 100); continue; }
            return false;
        }
        b += w;
        n -= (int)w;
    }
    return true;
}

static bool send_cmd(const TS *t)
{
    u8 hdr[20] = {0};
    be32(hdr, (u32)t->n);
    be32(hdr + 4, CTRL_CHANNEL);
    return send_all(hdr, 20) && send_all(t->d, t->n);
}

/* wait for the reply to `tag`; returns pointer to the payload after cmd/tag */
static const u8 *wait_reply(u32 tag, int *len)
{
    static u8 pkt[1 << 16];
    for (int tries = 0; tries < 200; tries++) {
        while (s_rlen >= 20) {
            u32 plen = rbe32(s_rbuf), chan = rbe32(s_rbuf + 4);
            if (plen > sizeof pkt) return NULL;
            if (s_rlen < 20 + (int)plen) break;
            memcpy(pkt, s_rbuf + 20, plen);
            memmove(s_rbuf, s_rbuf + 20 + plen, (size_t)(s_rlen - 20 - (int)plen));
            s_rlen -= 20 + (int)plen;
            if (chan != CTRL_CHANNEL || plen < 10) continue;
            u32 cmd = rbe32(pkt + 1), t = rbe32(pkt + 6);
            if (t != tag) {
                if (cmd == CMD_REQUEST && plen >= 20) s_requested += rbe32(pkt + 16);
                continue;
            }
            if (cmd != CMD_REPLY) return NULL;
            *len = (int)plen - 10;
            return pkt + 10;
        }
        struct pollfd pf = {s_fd, POLLIN, 0};
        if (poll(&pf, 1, 10) <= 0) continue;
        ssize_t n = read(s_fd, s_rbuf + s_rlen, sizeof s_rbuf - (size_t)s_rlen);
        if (n <= 0) return NULL;
        s_rlen += (int)n;
    }
    return NULL;
}

static bool pa_connect(void)
{
    char path[256];
    const char *srv = getenv("PULSE_SERVER");
    if (srv && !strncmp(srv, "unix:", 5)) snprintf(path, sizeof path, "%s", srv + 5);
    else if (srv && srv[0] == '/') snprintf(path, sizeof path, "%s", srv);
    else {
        const char *rt = getenv("XDG_RUNTIME_DIR");
        if (!rt) return false;
        snprintf(path, sizeof path, "%s/pulse/native", rt);
    }
    s_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s_fd < 0) return false;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof sa.sun_path) { close(s_fd); s_fd = -1; return false; }
    memcpy(sa.sun_path, path, strlen(path) + 1);
    if (connect(s_fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(s_fd); s_fd = -1; return false; }

    u8 cookie[256] = {0};
    char cpath[512];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(cpath, sizeof cpath, "%s/.config/pulse/cookie", home);
        FILE *f = fopen(cpath, "rb");
        if (f) { if (fread(cookie, 1, sizeof cookie, f) != sizeof cookie) memset(cookie, 0, sizeof cookie); fclose(f); }
    }
    int len;
    TS t = {0};
    ts_u32(&t, CMD_AUTH); ts_u32(&t, 0);
    ts_u32(&t, PA_VERSION);
    ts_arb(&t, cookie, sizeof cookie);
    if (!send_cmd(&t) || !wait_reply(0, &len)) return false;

    t.n = 0;
    ts_u32(&t, CMD_SET_CLIENT_NAME); ts_u32(&t, 1);
    ts_props(&t, "application.name", "Noctis");
    if (!send_cmd(&t) || !wait_reply(1, &len)) return false;

    const u32 bytes_per_sec = AUDIO_RATE * 4;
    t.n = 0;
    ts_u32(&t, CMD_CREATE_PLAYBACK_STREAM); ts_u32(&t, 2);
    u8 ss[7] = {'a', 3 /* S16LE */, 2, 0, 0, 0, 0};
    be32(ss + 3, AUDIO_RATE);
    ts_raw(&t, ss, 7);
    u8 cm[4] = {'m', 2, 1, 2};
    ts_raw(&t, cm, 4);
    ts_u32(&t, 0xffffffffu);   /* sink index */
    ts_null(&t);                /* sink name */
    ts_u32(&t, 0xffffffffu);   /* maxlength */
    ts_bool(&t, false);         /* corked */
    ts_u32(&t, bytes_per_sec * 50 / 1000);  /* tlength: 50ms */
    ts_u32(&t, 0xffffffffu);   /* prebuf */
    ts_u32(&t, bytes_per_sec * 10 / 1000);  /* minreq */
    ts_u32(&t, 0);              /* sync id */
    u8 vol[10] = {'v', 2};
    be32(vol + 2, 0x10000);
    be32(vol + 6, 0x10000);
    ts_raw(&t, vol, 10);
    for (int i = 0; i < 7; i++) ts_bool(&t, false); /* v12 flags */
    ts_bool(&t, false);         /* muted */
    ts_bool(&t, true);          /* adjust latency */
    ts_props(&t, "media.name", "Noctis");
    if (!send_cmd(&t)) return false;
    const u8 *r = wait_reply(2, &len);
    if (!r || len < 15 || r[0] != 'L' || r[5] != 'L' || r[10] != 'L') return false;
    s_channel = rbe32(r + 1);
    s_requested += rbe32(r + 11);
    fcntl(s_fd, F_SETFL, fcntl(s_fd, F_GETFL) | O_NONBLOCK);
    return true;
}

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
static void handle_incoming(void)
{
    for (;;) {
        ssize_t n = read(s_fd, s_rbuf + s_rlen, sizeof s_rbuf - (size_t)s_rlen);
        if (n > 0) { s_rlen += (int)n; continue; }
        if (n == 0) { s_ok = false; return; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        s_ok = false;
        return;
    }
    while (s_rlen >= 20) {
        u32 plen = rbe32(s_rbuf), chan = rbe32(s_rbuf + 4);
        if (plen > sizeof s_rbuf - 20) { s_ok = false; return; }
        if (s_rlen < 20 + (int)plen) break;
        const u8 *p = s_rbuf + 20;
        if (chan == CTRL_CHANNEL && plen >= 10) {
            u32 cmd = rbe32(p + 1);
            if (cmd == CMD_REQUEST && plen >= 20 && rbe32(p + 11) == s_channel) s_requested += rbe32(p + 16);
            else if (cmd == CMD_ERROR) { fprintf(stderr, "audio: server error\n"); s_ok = false; }
        }
        memmove(s_rbuf, s_rbuf + 20 + plen, (size_t)(s_rlen - 20 - (int)plen));
        s_rlen -= 20 + (int)plen;
    }
}

static void flush_out(void)
{
    while (s_wlen > 0) {
        ssize_t w = write(s_fd, s_wbuf, (size_t)s_wlen);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) s_ok = false;
            return;
        }
        memmove(s_wbuf, s_wbuf + w, (size_t)(s_wlen - w));
        s_wlen -= (int)w;
    }
}

void audio_update(void)
{
    if (!s_ok) return;
    s_music_target = s_music_on ? 1.0f : 0.0f;
    handle_incoming();
    flush_out();
    static i16 tmp[4096];
    while (s_ok && s_requested >= 4) {
        u32 n = s_requested > sizeof tmp ? (u32)sizeof tmp : s_requested;
        n &= ~3u;
        if (s_wlen + 20 + (int)n > (int)sizeof s_wbuf) break;
        mix(tmp, (int)(n / 4));
        u8 *h = s_wbuf + s_wlen;
        memset(h, 0, 20);
        be32(h, n);
        be32(h + 4, s_channel);
        memcpy(h + 20, tmp, n);
        s_wlen += 20 + (int)n;
        s_requested -= n;
    }
    flush_out();
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
    if (!pa_connect()) {
        if (s_fd >= 0) close(s_fd);
        s_fd = -1;
        fprintf(stderr, "audio: no PulseAudio server, running silent\n");
        return false;
    }
    reverb_init();
    sfx_generate(s_snd);
    s_ok = true;
    return true;
}

void audio_shutdown(void)
{
    if (s_fd >= 0) close(s_fd);
    s_fd = -1;
    s_ok = false;
}
