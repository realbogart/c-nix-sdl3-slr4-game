/*
 * Procedural art: a 256 colour palette built from 16 ramps, light
 * colormaps, translucency tables, and every texture / sprite / weapon
 * image in the game, painted with simple shaded primitives.
 */
#include "common.h"

u32 g_pal[256];
u8 g_cmap[NLIGHT][256];
u8 g_blend[256 * 256];
u8 g_addt[256 * 256];

Image g_walls[WT_COUNT];
Image g_flats[FT_COUNT];
Image g_spr[SPR_COUNT];
Image g_wpn[WPN_COUNT];
Image g_sky[2];

static bool s_fullbright[256];
static int s_pr[256], s_pg[256], s_pb[256];

/* dark, mid, bright key colours of each ramp */
static const u8 RAMPS[16][3][3] = {
    {{0, 0, 0}, {118, 116, 112}, {236, 232, 224}},     /* gray */
    {{14, 10, 8}, {116, 100, 84}, {222, 204, 176}},    /* warm stone */
    {{8, 10, 14}, {84, 94, 112}, {190, 200, 216}},     /* cool stone */
    {{12, 7, 3}, {104, 62, 30}, {208, 154, 96}},       /* wood */
    {{16, 0, 0}, {150, 12, 12}, {255, 116, 96}},       /* blood */
    {{26, 8, 0}, {226, 106, 12}, {255, 238, 164}},     /* fire */
    {{18, 12, 0}, {196, 156, 30}, {255, 248, 190}},    /* gold */
    {{20, 18, 12}, {168, 158, 128}, {255, 250, 234}},  /* bone */
    {{2, 8, 2}, {58, 98, 38}, {178, 220, 138}},        /* moss */
    {{8, 10, 6}, {100, 116, 74}, {208, 224, 170}},     /* ghoul */
    {{0, 2, 18}, {30, 62, 186}, {166, 206, 255}},      /* blue */
    {{8, 0, 16}, {100, 30, 142}, {224, 172, 255}},     /* purple */
    {{16, 0, 8}, {174, 20, 92}, {255, 152, 212}},      /* crimson */
    {{18, 8, 6}, {176, 104, 84}, {255, 214, 194}},     /* flesh */
    {{0, 10, 12}, {36, 142, 152}, {192, 255, 250}},    /* spectre */
    {{2, 2, 10}, {32, 34, 76}, {150, 158, 216}},       /* night */
};

/* ------------------------------------------------------------------ */
/* palette & tables                                                    */
/* ------------------------------------------------------------------ */
static void build_palette(void)
{
    for (int r = 0; r < 16; r++) {
        for (int s = 0; s < 16; s++) {
            int i = r * 16 + s;
            float t = s / 15.0f, u;
            const u8 *a, *b;
            if (t < 0.5f) { a = RAMPS[r][0]; b = RAMPS[r][1]; u = t * 2.0f; }
            else { a = RAMPS[r][1]; b = RAMPS[r][2]; u = (t - 0.5f) * 2.0f; }
            s_pr[i] = (int)(a[0] + (b[0] - a[0]) * u + 0.5f);
            s_pg[i] = (int)(a[1] + (b[1] - a[1]) * u + 0.5f);
            s_pb[i] = (int)(a[2] + (b[2] - a[2]) * u + 0.5f);
            g_pal[i] = (u32)(s_pr[i] << 16 | s_pg[i] << 8 | s_pb[i]);
        }
    }
    g_pal[TRANSP] = 0xff00ff;
    static const int fb[] = {R_BLOOD, R_FIRE, R_GOLD, R_BLUE, R_PURPLE, R_CRIMSON, R_SPECTRE};
    for (size_t k = 0; k < sizeof fb / sizeof fb[0]; k++)
        for (int s = 13; s < 16; s++) s_fullbright[fb[k] * 16 + s] = true;
}

u8 pal_nearest(int r, int g, int b)
{
    int best = 0, bd = 0x7fffffff;
    for (int i = 0; i < 255; i++) {
        int dr = r - s_pr[i], dg = g - s_pg[i], db = b - s_pb[i];
        int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return (u8)best;
}

u8 pal_index(int ramp, int shade)
{
    if (shade < 0) shade = 0;
    if (shade > 15) shade = 15;
    if (ramp == R_NIGHT && shade > 14) shade = 14;
    return (u8)(ramp * 16 + shade);
}

static void build_tables(void)
{
    for (int l = 0; l < NLIGHT; l++) {
        float f = l / LIGHT_ONE;
        float fog = 1.0f - minf(f, 1.0f);
        for (int i = 0; i < 255; i++) {
            if (s_fullbright[i]) { g_cmap[l][i] = (u8)i; continue; }
            int r = (int)(s_pr[i] * f + 7 * fog);
            int g = (int)(s_pg[i] * f + 5 * fog);
            int b = (int)(s_pb[i] * f + 12 * fog);
            g_cmap[l][i] = pal_nearest(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255));
        }
        g_cmap[l][TRANSP] = TRANSP;
    }
    for (int a = 0; a < 256; a++) {
        for (int b = a; b < 256; b++) {
            u8 bl, ad;
            if (a == TRANSP || b == TRANSP) {
                bl = ad = (u8)(a == TRANSP ? b : a);
            } else {
                bl = pal_nearest((s_pr[a] + s_pr[b]) / 2, (s_pg[a] + s_pg[b]) / 2, (s_pb[a] + s_pb[b]) / 2);
                ad = pal_nearest(clampi(s_pr[a] + s_pr[b], 0, 255), clampi(s_pg[a] + s_pg[b], 0, 255),
                                 clampi(s_pb[a] + s_pb[b], 0, 255));
            }
            g_blend[a * 256 + b] = g_blend[b * 256 + a] = bl;
            g_addt[a * 256 + b] = g_addt[b * 256 + a] = ad;
        }
    }
}

/* ------------------------------------------------------------------ */
/* noise                                                               */
/* ------------------------------------------------------------------ */
static u32 hash3(int x, int y, int s)
{
    u32 h = (u32)x * 0x8da6b343u ^ (u32)y * 0xd8163841u ^ (u32)s * 0xcb1ab31fu;
    h ^= h >> 15; h *= 0x2c1b3c6du;
    h ^= h >> 12; h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

static float hashf(int x, int y, int s) { return (hash3(x, y, s) & 0xffffff) / 16777216.0f; }

static float vnoise(float x, float y, int px, int py, int seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    int x0 = ((xi % px) + px) % px, x1 = (x0 + 1) % px;
    int y0 = ((yi % py) + py) % py, y1 = (y0 + 1) % py;
    float a = hashf(x0, y0, seed), b = hashf(x1, y0, seed);
    float c = hashf(x0, y1, seed), d = hashf(x1, y1, seed);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

/* tileable fbm: (x,y) in texels, texture spans (w,h) with (cx,cy) lattice cells */
static float fbm(float x, float y, int w, int h, int cx, int cy, int oct, int seed)
{
    float fx = x * cx / w, fy = y * cy / h;
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; i++) {
        s += a * vnoise(fx, fy, cx, cy, seed + i * 101);
        n += a;
        fx *= 2; fy *= 2; cx *= 2; cy *= 2; a *= 0.5f;
    }
    return s / n;
}

/* ------------------------------------------------------------------ */
/* painting primitives                                                 */
/* ------------------------------------------------------------------ */
static const u8 BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static inline float dith(int x, int y) { return (BAYER[y & 3][x & 3] + 0.5f) / 16.0f - 0.5f; }

static Image img_new(int w, int h)
{
    Image im = {w, h, malloc((size_t)w * h)};
    memset(im.px, TRANSP, (size_t)w * h);
    return im;
}

static inline void pset(Image *im, int x, int y, u8 c)
{
    if ((unsigned)x < (unsigned)im->w && (unsigned)y < (unsigned)im->h) im->px[x * im->h + y] = c;
}

static inline u8 pget(const Image *im, int x, int y)
{
    if ((unsigned)x < (unsigned)im->w && (unsigned)y < (unsigned)im->h) return im->px[x * im->h + y];
    return TRANSP;
}

static inline u8 SH(int ramp, float s, int x, int y)
{
    return pal_index(ramp, (int)floorf(s + 0.5f + dith(x, y)));
}

static void shift_px(Image *im, int x, int y, int amt)
{
    u8 c = pget(im, x, y);
    if (c == TRANSP || s_fullbright[c]) return;
    pset(im, x, y, pal_index(c >> 4, (c & 15) + amt));
}

static const float LX = -0.45f, LY = -0.6f, LZ = 0.66f;

static void ellipse(Image *im, float cx, float cy, float rx, float ry, int ramp, float s0, float s1)
{
    for (int y = (int)floorf(cy - ry); y <= (int)ceilf(cy + ry); y++)
        for (int x = (int)floorf(cx - rx); x <= (int)ceilf(cx + rx); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            float d2 = dx * dx + dy * dy;
            if (d2 > 1.0f) continue;
            float lam = clampf(dx * LX + dy * LY + sqrtf(1 - d2) * LZ, 0, 1);
            pset(im, x, y, SH(ramp, s0 + (s1 - s0) * lam, x, y));
        }
}

static void ellipse_c(Image *im, float cx, float cy, float rx, float ry, u8 c)
{
    for (int y = (int)floorf(cy - ry); y <= (int)ceilf(cy + ry); y++)
        for (int x = (int)floorf(cx - rx); x <= (int)ceilf(cx + rx); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) pset(im, x, y, c);
        }
}

static void ring(Image *im, float cx, float cy, float r0, float r1, u8 c)
{
    for (int y = (int)floorf(cy - r1); y <= (int)ceilf(cy + r1); y++)
        for (int x = (int)floorf(cx - r1); x <= (int)ceilf(cx + r1); x++) {
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
            if (d >= r0 && d <= r1) pset(im, x, y, c);
        }
}

static void capsule(Image *im, float x0, float y0, float x1, float y1, float r0, float r1,
                    int ramp, float s0, float s1)
{
    float mr = maxf(r0, r1);
    int xa = (int)floorf(minf(x0, x1) - mr), xb = (int)ceilf(maxf(x0, x1) + mr);
    int ya = (int)floorf(minf(y0, y1) - mr), yb = (int)ceilf(maxf(y0, y1) + mr);
    float vx = x1 - x0, vy = y1 - y0, vv = vx * vx + vy * vy;
    if (vv < 1e-6f) vv = 1e-6f;
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float t = clampf(((px - x0) * vx + (py - y0) * vy) / vv, 0, 1);
            float r = lerpf(r0, r1, t);
            float ox = (px - (x0 + vx * t)) / r, oy = (py - (y0 + vy * t)) / r;
            float d2 = ox * ox + oy * oy;
            if (d2 > 1.0f) continue;
            float lam = clampf(ox * LX + oy * LY + sqrtf(1 - d2) * LZ, 0, 1);
            pset(im, x, y, SH(ramp, s0 + (s1 - s0) * lam, x, y));
        }
}

static void line_c(Image *im, float x0, float y0, float x1, float y1, u8 c)
{
    int n = (int)ceilf(maxf(fabsf(x1 - x0), fabsf(y1 - y0))) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        pset(im, (int)floorf(lerpf(x0, x1, t)), (int)floorf(lerpf(y0, y1, t)), c);
    }
}

typedef u8 (*PixFn)(int x, int y, const void *ctx);

/* even-odd scanline polygon fill */
static void poly(Image *im, const float *pts, int n, PixFn fn, const void *ctx)
{
    float ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; i++) { ymin = minf(ymin, pts[i * 2 + 1]); ymax = maxf(ymax, pts[i * 2 + 1]); }
    for (int y = (int)floorf(ymin); y <= (int)ceilf(ymax); y++) {
        float sy = y + 0.5f, xs[64];
        int nx = 0;
        for (int i = 0; i < n && nx < 64; i++) {
            float ax = pts[i * 2], ay = pts[i * 2 + 1];
            float bx = pts[((i + 1) % n) * 2], by = pts[((i + 1) % n) * 2 + 1];
            if ((ay <= sy && by > sy) || (by <= sy && ay > sy))
                xs[nx++] = ax + (sy - ay) / (by - ay) * (bx - ax);
        }
        for (int i = 1; i < nx; i++)
            for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
        for (int i = 0; i + 1 < nx; i += 2)
            for (int x = (int)ceilf(xs[i] - 0.5f); x <= (int)floorf(xs[i + 1] - 0.5f); x++)
                pset(im, x, y, fn(x, y, ctx));
    }
}

typedef struct { int ramp; float s0, s1, ax, ay, bx, by, noise; int seed; } Grad;

static u8 grad_fn(int x, int y, const void *c)
{
    const Grad *g = c;
    float vx = g->bx - g->ax, vy = g->by - g->ay, vv = vx * vx + vy * vy;
    float t = vv > 0 ? clampf(((x + 0.5f - g->ax) * vx + (y + 0.5f - g->ay) * vy) / vv, 0, 1) : 0;
    float s = lerpf(g->s0, g->s1, t) + (hashf(x, y, g->seed) - 0.5f) * g->noise;
    return SH(g->ramp, s, x, y);
}

static void poly_grad(Image *im, const float *pts, int n, int ramp, float s0, float s1,
                      float ax, float ay, float bx, float by, float noise)
{
    Grad g = {ramp, s0, s1, ax, ay, bx, by, noise, 17};
    poly(im, pts, n, grad_fn, &g);
}

/* darken the silhouette edge of a sprite for a crisp retro outline */
static void outline(Image *im, int amt)
{
    u8 *copy = malloc((size_t)im->w * im->h);
    memcpy(copy, im->px, (size_t)im->w * im->h);
    for (int x = 0; x < im->w; x++)
        for (int y = 0; y < im->h; y++) {
            u8 c = copy[x * im->h + y];
            if (c == TRANSP || s_fullbright[c]) continue;
            bool edge = false;
            static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (int k = 0; k < 4; k++) {
                int nx = x + d[k][0], ny = y + d[k][1];
                if ((unsigned)nx >= (unsigned)im->w || (unsigned)ny >= (unsigned)im->h ||
                    copy[nx * im->h + ny] == TRANSP) { edge = true; break; }
            }
            if (edge) im->px[x * im->h + y] = pal_index(c >> 4, (c & 15) - amt);
        }
    free(copy);
}

/* ------------------------------------------------------------------ */
/* wall textures (64 x 96)                                             */
/* ------------------------------------------------------------------ */
#define TW 64
#define TH 96

static void tex_stone(Image *im, int ramp, int seed, float base, float contrast)
{
    const int RH = 16;
    int W = im->w, H = im->h;
    for (int r = 0; r < H / RH; r++) {
        int nb = 2 + (int)(hash3(r, 0, seed) % 2);
        int off = (int)(hash3(r, 1, seed) % 64);
        int bounds[5];
        bounds[0] = 0;
        if (nb == 2) { bounds[1] = 26 + (int)(hash3(r, 2, seed) % 13); bounds[2] = 64; }
        else {
            bounds[1] = 18 + (int)(hash3(r, 2, seed) % 8);
            bounds[2] = bounds[1] + 18 + (int)(hash3(r, 3, seed) % 8);
            bounds[3] = 64;
        }
        for (int x = 0; x < W; x++) {
            int rel = (x - off + 64) % 64, seg = 0;
            while (rel >= bounds[seg + 1]) seg++;
            int lx = rel - bounds[seg], bw = bounds[seg + 1] - bounds[seg];
            float bb = base + (hashf(r, seg, seed + 7) - 0.5f) * 2.4f * contrast;
            for (int ly = 0; ly < RH; ly++) {
                int y = r * RH + ly;
                float s;
                if (lx == 0 || ly == 0) {
                    s = base * 0.22f + hashf(x, y, seed) * 0.8f;
                } else {
                    float n = fbm((float)x, (float)y, W, H, 8, 12, 4, seed) - 0.5f;
                    s = bb + n * 3.4f * contrast;
                    if (ly == 1 || lx == 1) s += 1.3f;
                    if (ly == RH - 1 || lx == bw - 1) s -= 1.5f;
                    if (hashf(x, y, seed + 3) < 0.05f) s -= 1.4f;
                }
                pset(im, x, y, SH(ramp, s, x, y));
            }
        }
    }
    /* cracks */
    for (int c = 0; c < 4; c++) {
        float x = hashf(c, 9, seed) * W, y = hashf(c, 10, seed) * H;
        int len = 8 + (int)(hashf(c, 11, seed) * 18);
        for (int i = 0; i < len; i++) {
            shift_px(im, ((int)x + W) % W, (int)y, -3);
            x += (hashf(c, i, seed + 5) - 0.5f) * 2.2f;
            y += 1;
        }
    }
}

static bool in_arch(float fx, float fy, float cx, float hw, float ys, float sill)
{
    if (fy > sill) return false;
    if (fy >= ys) return fabsf(fx - cx) <= hw;
    float R = 2 * hw;
    return hypotf(fx - (cx - hw + R), fy - ys) <= R && hypotf(fx - (cx + hw - R), fy - ys) <= R;
}

static void tex_window(Image *im)
{
    tex_stone(im, R_COOL, 31, 6.0f, 1.0f);
    const float cx = 32, ys = 42, sill = 86;
    static const int glass[] = {R_BLUE, R_BLUE, R_CRIMSON, R_GOLD, R_PURPLE, R_BLUE, R_CRIMSON, R_SPECTRE};
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            if (!in_arch(fx, fy, cx, 17, ys, sill + 3)) continue;
            if (!in_arch(fx, fy, cx, 13, ys, sill)) {
                /* carved frame */
                float s = 8.5f + (fbm(fx, fy, TW, TH, 8, 12, 3, 5) - 0.5f) * 2.5f;
                if (!in_arch(fx + 1, fy + 1, cx, 17, ys, sill + 3)) s -= 2.5f;
                if (in_arch(fx - 1, fy - 1, cx, 13, ys, sill)) s -= 2.0f;
                pset(im, x, y, SH(R_COOL, s, x, y));
                continue;
            }
            float u = fx - cx, v = fy;
            float d1 = (u + v) / 6.5f, d2 = (u - v) / 6.5f;
            bool lead = (d1 - floorf(d1)) < 0.16f || (d2 - floorf(d2)) < 0.16f;
            if (fabsf(u) < 1.0f || fabsf(fy - 64) < 1.0f) lead = true;
            if (!in_arch(fx, fy, cx, 12, ys, sill - 1)) lead = true;
            if (fy < ys) {
                float rd = hypotf(u, fy - (ys - 9));
                if (fabsf(rd - 6.5f) < 0.8f) lead = true;
                else if (rd < 6.5f) {
                    lead = false;
                    int col = rd < 2.5f ? R_GOLD : R_CRIMSON;
                    pset(im, x, y, pal_index(col, 13 + (int)(hashf(x, y, 3) * 2.99f)));
                    continue;
                }
            }
            if (lead) { pset(im, x, y, pal_index(R_GRAY, 1 + (hashf(x, y, 1) < 0.3f))); continue; }
            int pane = (int)(hash3((int)floorf(d1), (int)floorf(d2), 99) % 8);
            int sh = 13 + (int)(fbm(fx, fy, TW, TH, 16, 24, 2, pane) * 3.2f);
            pset(im, x, y, pal_index(glass[pane], sh));
        }
}

static void tex_wood(Image *im)
{
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++) {
            int plank = x / 16, lx = x % 16;
            float s = 5.0f + (hashf(plank, 0, 77) - 0.5f) * 1.6f;
            s += (fbm(x * 3.0f, y * 0.25f, TW * 3, TH / 4, 8, 6, 3, 41 + plank) - 0.5f) * 4.0f;
            if (lx == 0) s = 1.0f;
            else if (lx == 1) s -= 1.2f;
            else if (lx == 15) s -= 0.8f;
            /* carved rails */
            int ry = y % 32;
            if (ry < 4) s = 6.5f + (ry == 0 ? 2.0f : ry == 3 ? -2.5f : 0.5f);
            if ((lx == 4 || lx == 11) && (ry == 8 || ry == 24)) s = 9.0f;
            pset(im, x, y, SH(R_WOOD, s, x, y));
        }
}

static void tex_moss(Image *im)
{
    tex_stone(im, R_WARM, 57, 6.0f, 1.0f);
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++) {
            float n = fbm((float)x, (float)y, TW, TH, 4, 6, 4, 202) + y / (float)TH * 0.35f;
            if (n > 0.72f) {
                float s = 3.0f + (n - 0.72f) * 18.0f + (hashf(x, y, 4) - 0.5f) * 2.5f;
                pset(im, x, y, SH(R_MOSS, s, x, y));
            }
        }
}

static void tex_blood(Image *im)
{
    tex_stone(im, R_COOL, 71, 5.5f, 1.0f);
    for (int d = 0; d < 9; d++) {
        float x = hashf(d, 0, 5) * TW;
        float y0 = hashf(d, 1, 5) * 40;
        float len = 18 + hashf(d, 2, 5) * 56;
        float w = 0.8f + hashf(d, 3, 5) * 1.6f;
        ellipse(im, x, y0, 3 + w * 2, 2.5f + w, R_BLOOD, 2, 6);
        for (float y = y0; y < y0 + len && y < TH; y += 0.5f) {
            float ww = w * (1.0f - (y - y0) / len * 0.6f);
            for (int k = (int)floorf(-ww); k <= (int)ceilf(ww); k++)
                pset(im, ((int)x + k + TW) % TW, (int)y, SH(R_BLOOD, 3.5f + hashf((int)x + k, (int)y, 1) * 2.0f, (int)x + k, (int)y));
        }
        ellipse(im, x, minf(y0 + len, TH - 2), w * 1.4f + 0.5f, w * 1.6f + 0.5f, R_BLOOD, 3, 7);
    }
}

static void tex_bones(Image *im)
{
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++)
            pset(im, x, y, SH(R_WARM, 1.5f + hashf(x, y, 8) * 1.2f, x, y));
    for (int row = 0; row < 6; row++) {
        float by = row * 16;
        if (row % 2 == 0) {
            /* skulls */
            for (int k = 0; k < 4; k++) {
                float cx = k * 16 + 8 + ((row / 2) % 2) * 8;
                for (int wrap = -1; wrap <= 1; wrap++) {
                    float sx = cx + wrap * 64;
                    ellipse(im, sx, by + 7, 6.3f, 5.8f, R_BONE, 4, 11);
                    ellipse(im, sx, by + 11.5f, 4.2f, 3.0f, R_BONE, 3, 9);
                    ellipse_c(im, sx - 2.5f, by + 7.5f, 1.7f, 1.9f, pal_index(R_GRAY, 0));
                    ellipse_c(im, sx + 2.5f, by + 7.5f, 1.7f, 1.9f, pal_index(R_GRAY, 0));
                    pset(im, (int)sx, (int)(by + 10), pal_index(R_GRAY, 1));
                    pset(im, (int)sx - 1, (int)(by + 10), pal_index(R_GRAY, 1));
                    for (int t = -3; t <= 2; t += 2) pset(im, (int)sx + t, (int)(by + 13), pal_index(R_BONE, 2));
                }
            }
        } else {
            /* stacked long bones seen end-on */
            for (int k = 0; k < 9; k++)
                for (int j = 0; j < 2; j++) {
                    float cx = k * 7.11f + 3.5f + j * 3.5f, cy = by + 4.5f + j * 7;
                    for (int wrap = -1; wrap <= 1; wrap++)
                        ellipse(im, cx + wrap * 64, cy, 3.4f, 3.2f, R_BONE, 3, 10 - j);
                }
            for (int x = 0; x < TW; x++) shift_px(im, x, (int)by + 15, -3);
        }
    }
}

static void tex_banner(Image *im)
{
    tex_stone(im, R_COOL, 88, 5.5f, 1.0f);
    for (int x = 14; x < 50; x++) {
        float bottom = 86 - fabsf(x + 0.5f - 32) * 0.55f;
        for (int y = 9; y < (int)bottom; y++) {
            float s = 6.0f + sinf(x * 0.7f) * 1.4f - fabsf(x - 31.5f) * 0.05f;
            s += (hashf(x, y, 2) - 0.5f) * 0.8f;
            int ramp = R_BLOOD;
            if (x < 16 || x > 47 || y > bottom - 3) { ramp = R_GOLD; s = 8 + sinf(x * 0.7f); }
            /* emblem: gold cross with ring */
            float ex = x + 0.5f - 32, ey = y + 0.5f - 42;
            float rd = hypotf(ex, ey);
            if ((fabsf(ex) < 2.2f && ey > -16 && ey < 22) || (fabsf(ey + 4) < 2.2f && fabsf(ex) < 10) ||
                fabsf(rd - 8) < 1.2f) {
                ramp = R_GOLD;
                s = 10 + sinf(x * 0.7f);
            }
            pset(im, x, y, SH(ramp, s, x, y));
        }
        /* shadow on wall right of banner */
        shift_px(im, 50, x + 10, -2);
    }
    capsule(im, 10, 8, 54, 8, 1.6f, 1.6f, R_GOLD, 4, 11);
    ellipse(im, 10, 8, 2.5f, 2.5f, R_GOLD, 5, 12);
    ellipse(im, 54, 8, 2.5f, 2.5f, R_GOLD, 5, 12);
}

static void tex_organ(Image *im)
{
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++) {
            float s = 2.2f + (fbm(x * 2.0f, y * 0.4f, TW * 2, TH * 2 / 5, 8, 8, 3, 12) - 0.5f) * 2.0f;
            if (y > 82) s = 4.5f + ((y - 82) % 7 == 0 ? -2 : 0) + (x % 16 == 0 ? -2 : 0);
            pset(im, x, y, SH(R_WOOD, s, x, y));
        }
    for (int i = 0; i < 9; i++) {
        float cx = 3.5f + i * 7.125f;
        float top = 8 + fabsf(i - 4.0f) * 6.0f;
        for (int x = (int)(cx - 3); x <= (int)(cx + 3); x++) {
            float ox = (x + 0.5f - cx) / 3.2f;
            if (fabsf(ox) > 1) continue;
            float lam = clampf(ox * LX + sqrtf(1 - ox * ox) * 0.9f, 0, 1);
            for (int y = (int)top; y < 72; y++) {
                float s = 3 + lam * 9;
                if (y >= 60 && y < 64 && fabsf(ox) < 0.6f) s = y == 60 ? 9 : 1;
                pset(im, x, y, SH(R_GOLD, s, x, y));
            }
            for (int y = 72; y < 82; y++) {
                float taper = 1.0f - (y - 72) / 12.0f;
                if (fabsf(ox) < taper) pset(im, x, y, SH(R_GOLD, 2 + lam * 6 * taper, x, y));
            }
        }
        ellipse(im, cx, top, 3.2f, 1.3f, R_GOLD, 4, 12);
    }
}

static void tex_door(Image *im, int band_ramp, int sigil_ramp)
{
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++) {
            int plank = x / 16, lx = x % 16;
            float s = 5.0f + (hashf(plank, 0, 33) - 0.5f) * 1.5f;
            s += (fbm(x * 3.0f, y * 0.25f, TW * 3, TH / 4, 8, 6, 3, 61 + plank) - 0.5f) * 3.6f;
            if (lx == 0) s = 1.0f;
            else if (lx == 1) s -= 1.0f;
            if (x < 3 || x > 60 || y < 3 || y > 92) s = 2.5f + hashf(x, y, 1);
            pset(im, x, y, SH(R_WOOD, s, x, y));
        }
    for (int b = 0; b < 2; b++) {
        int y0 = b ? 74 : 14;
        for (int x = 3; x < 61; x++)
            for (int y = y0; y < y0 + 7; y++) {
                float s = 6 + (y == y0 ? 3 : y == y0 + 6 ? -3 : 0) + (hashf(x, y, 7) - 0.5f) * 1.5f;
                if (band_ramp == R_BLOOD) s -= 1.5f;
                pset(im, x, y, SH(band_ramp, s, x, y));
            }
        for (int k = 0; k < 4; k++) ellipse(im, 8 + k * 16, y0 + 3.5f, 1.6f, 1.6f, band_ramp, 4, 12);
    }
    ellipse(im, 48, 47, 3, 3, R_GRAY, 3, 8);
    ring(im, 48, 54, 4.5f, 6.0f, pal_index(R_GRAY, 6));
    for (int k = 0; k < 6; k++) shift_px(im, 44 + k, 60, -2);
    if (sigil_ramp >= 0) {
        ring(im, 30, 47, 8, 10.2f, pal_index(sigil_ramp, 13));
        ring(im, 30, 47, 9, 9.6f, pal_index(sigil_ramp, 15));
        for (int y = 38; y < 57; y++) {
            pset(im, 29, y, pal_index(sigil_ramp, 14));
            pset(im, 30, y, pal_index(sigil_ramp, 15));
        }
        for (int x = 23; x < 38; x++) pset(im, x, 44, pal_index(sigil_ramp, 14));
    }
}

static void tex_jamb(Image *im)
{
    tex_stone(im, R_COOL, 140, 4.5f, 0.8f);
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++) {
            int lx = x % 32;
            if (lx >= 12 && lx < 20) {
                float ox = (lx - 15.5f) / 4;
                float s = 4 + (1 - ox * ox) * 5 + (ox < 0 ? 1 : -1);
                pset(im, x, y, SH(R_COOL, s, x, y));
            }
        }
}

static void tex_exit(Image *im)
{
    tex_stone(im, R_GRAY, 160, 3.5f, 0.9f);
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++) {
            float d = hypotf(x + 0.5f - 32, y + 0.5f - 48);
            if (d < 30) shift_px(im, x, y, d < 26 ? 2 : 1);
        }
    ring(im, 32, 48, 22, 25, pal_index(R_GOLD, 13));
    ring(im, 32, 48, 23, 24, pal_index(R_GOLD, 15));
    for (int k = 0; k < 12; k++) {
        float a = k * TAU / 12;
        ellipse_c(im, 32 + cosf(a) * 18.5f, 48 + sinf(a) * 18.5f, 1.3f, 1.3f, pal_index(R_GOLD, 15));
    }
    for (int y = 30; y < 68; y++)
        for (int x = 29; x < 35; x++) pset(im, x, y, pal_index(R_SPECTRE, x == 31 || x == 32 ? 15 : 13));
    for (int x = 18; x < 46; x++)
        for (int y = 39; y < 45; y++) pset(im, x, y, pal_index(R_SPECTRE, y == 41 || y == 42 ? 15 : 13));
}

/* ------------------------------------------------------------------ */
/* flats (64 x 64)                                                     */
/* ------------------------------------------------------------------ */
#define FS 64

typedef struct { float x, y; } V2;

static void voronoi(float x, float y, const V2 *p, int n, int *id, float *d1, float *d2)
{
    float b1 = 1e9f, b2 = 1e9f;
    int bi = 0;
    for (int i = 0; i < n; i++) {
        float dx = fabsf(x - p[i].x), dy = fabsf(y - p[i].y);
        if (dx > FS / 2) dx = FS - dx;
        if (dy > FS / 2) dy = FS - dy;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < b1) { b2 = b1; b1 = d; bi = i; }
        else if (d < b2) b2 = d;
    }
    *id = bi; *d1 = b1; *d2 = b2;
}

static void flat_stones(Image *im, int ramp, int npts, int seed, float base, bool domed)
{
    V2 p[40];
    for (int i = 0; i < npts; i++) { p[i].x = hashf(i, 0, seed) * FS; p[i].y = hashf(i, 1, seed) * FS; }
    for (int x = 0; x < FS; x++)
        for (int y = 0; y < FS; y++) {
            int id; float d1, d2;
            voronoi(x + 0.5f, y + 0.5f, p, npts, &id, &d1, &d2);
            float s;
            if (d2 - d1 < 1.3f) s = base * 0.25f;
            else {
                s = base + (hashf(id, 2, seed) - 0.5f) * 2.5f;
                s += (fbm((float)x, (float)y, FS, FS, 8, 8, 3, seed) - 0.5f) * 3.0f;
                if (domed) s += 2.5f - d1 * 0.45f;
                else if (d2 - d1 < 3.0f) s -= 0.9f;
            }
            pset(im, x, y, SH(ramp, s, x, y));
        }
}

static void flat_marble(Image *im)
{
    for (int x = 0; x < FS; x++)
        for (int y = 0; y < FS; y++) {
            bool white = ((x / 32) + (y / 32)) % 2 == 0;
            float n = fbm((float)x, (float)y, FS, FS, 4, 4, 5, white ? 3 : 4);
            float vein = fabsf(n - 0.5f);
            float s;
            if (white) s = 8.5f - (vein < 0.03f ? 3.5f : 0) + (n - 0.5f) * 1.5f;
            else s = 2.2f + (vein < 0.025f ? 3.0f : 0) + (n - 0.5f) * 1.0f;
            if (x % 32 == 0 || y % 32 == 0) s = 1.0f;
            pset(im, x, y, SH(white ? R_BONE : R_GRAY, s, x, y));
        }
}

static void flat_woodfloor(Image *im)
{
    for (int x = 0; x < FS; x++)
        for (int y = 0; y < FS; y++) {
            int row = y / 8, ly = y % 8;
            int off = (int)(hash3(row, 0, 5) % 64);
            int seg = ((x + off) % 64) / 32;
            float s = 5.0f + (hashf(row, seg, 6) - 0.5f) * 2.0f;
            s += (fbm(x * 0.25f, y * 3.0f, FS / 4, FS * 3, 6, 8, 3, 12 + row) - 0.5f) * 3.5f;
            if (ly == 0 || ((x + off) % 32) == 0) s = 1.0f;
            pset(im, x, y, SH(R_WOOD, s, x, y));
        }
}

static void flat_dirt(Image *im)
{
    for (int x = 0; x < FS; x++)
        for (int y = 0; y < FS; y++) {
            float n = fbm((float)x, (float)y, FS, FS, 4, 4, 5, 51);
            float s = 2.5f + n * 3.5f + (hashf(x, y, 2) - 0.5f) * 1.5f;
            int ramp = R_WOOD;
            float g = fbm((float)x, (float)y, FS, FS, 8, 8, 3, 52);
            if (g > 0.6f && hashf(x, y, 9) < 0.7f) { ramp = R_MOSS; s = 2 + (g - 0.6f) * 16 + hashf(x, y, 3) * 2; }
            if (hashf(x, y, 77) < 0.012f) { ramp = R_GRAY; s = 7; }
            pset(im, x, y, SH(ramp, s, x, y));
        }
}

static void flat_carpet(Image *im)
{
    for (int x = 0; x < FS; x++)
        for (int y = 0; y < FS; y++) {
            int lx = x % 16, ly = y % 16;
            int d = abs(lx - 8) + abs(ly - 8);
            float s = 4.5f + (hashf(x, y, 1) - 0.5f) * 1.0f;
            int ramp = R_BLOOD;
            if (d == 6 || d == 2) { ramp = R_GOLD; s = 6.5f; }
            if (d == 0) { ramp = R_GOLD; s = 9; }
            s += (fbm((float)x, (float)y, FS, FS, 4, 4, 3, 9) - 0.5f) * 2.0f;
            pset(im, x, y, SH(ramp, s, x, y));
        }
}

static void flat_bloodstone(Image *im)
{
    flat_stones(im, R_WARM, 9, 300, 5.0f, false);
    for (int x = 0; x < FS; x++)
        for (int y = 0; y < FS; y++) {
            float n = fbm((float)x, (float)y, FS, FS, 4, 4, 4, 301);
            if (n > 0.62f) pset(im, x, y, SH(R_BLOOD, 2.0f + (n - 0.62f) * 20, x, y));
        }
}

static void flat_beams(Image *im)
{
    for (int x = 0; x < FS; x++)
        for (int y = 0; y < FS; y++) {
            int ly = y % 32;
            float s;
            if (ly < 10) {
                s = 4.5f + (ly == 0 ? 2 : ly == 9 ? -2.5f : 0);
                s += (fbm(x * 0.25f, (float)y, FS / 4, FS, 4, 8, 3, 88) - 0.5f) * 2.5f;
            } else {
                s = 1.8f + (fbm(x * 3.0f, y * 0.25f, FS * 3, FS / 4, 8, 4, 3, 89) - 0.5f) * 2.0f;
                if (x % 16 == 0) s = 0.5f;
            }
            pset(im, x, y, SH(R_WOOD, s, x, y));
        }
}

static void flat_vault(Image *im)
{
    for (int x = 0; x < FS; x++)
        for (int y = 0; y < FS; y++) {
            float s = 3.0f + (fbm((float)x, (float)y, FS, FS, 8, 8, 4, 401) - 0.5f) * 3.0f;
            float fx = x + 0.5f, fy = y + 0.5f;
            float dr = minf(fabsf(fx - fy), fabsf(fx + fy - FS)) * 0.7071f;
            float de = minf(minf(fx, FS - fx), minf(fy, FS - fy));
            float rib = minf(dr, de);
            if (rib < 2.2f) s = 7.5f - rib * 1.5f;
            float dc = hypotf(fx - 32, fy - 32);
            if (dc < 4.5f) s = 9.5f - dc;
            pset(im, x, y, SH(R_COOL, s, x, y));
        }
}

/* ------------------------------------------------------------------ */
/* sky (SKY_W x SKY_H, wraps twice around the horizon)                 */
/* ------------------------------------------------------------------ */
static void gen_sky(Image *im, bool blood)
{
    int base_ramp = blood ? R_BLOOD : R_NIGHT;
    float mx = 330, my = 62, mr = 24;
    for (int x = 0; x < SKY_W; x++) {
        /* distant silhouettes: hills + gothic spires */
        float hill = 10 + fbm((float)x, 0, SKY_W, 1, 8, 1, 4, blood ? 7 : 6) * 26;
        int sp = x / 64;
        float spx = sp * 64 + 16 + hashf(sp, 0, 3) * 32;
        float sph = 30 + hashf(sp, 1, 3) * 34;
        float spw = 3 + hashf(sp, 2, 3) * 4;
        if (hashf(sp, 4, 3) < 0.55f) {
            float d = fabsf(x + 0.5f - spx);
            if (d < spw) hill = maxf(hill, sph * 0.55f + (sph * 0.45f) * (1.0f - d / spw) * (d < 1.2f ? 1.4f : 1.0f));
        }
        for (int y = 0; y < SKY_H; y++) {
            float t = (float)y / SKY_HORIZON;
            float s = blood ? 0.5f + t * t * 7.0f : 0.5f + t * 4.5f;
            int ramp = base_ramp;
            float dm = hypotf(x + 0.5f - mx, y + 0.5f - my);
            if (dm < mr * 2.2f) s += (1.0f - dm / (mr * 2.2f)) * 3.0f;
            u8 c = SH(ramp, s, x, y);
            if (!blood && y < 140 && hashf(x, y, 21) < 0.006f)
                c = pal_index(R_GRAY, 9 + (int)(hashf(x, y, 22) * 6));
            if (dm < mr) {
                float n = fbm((float)x, (float)y, SKY_W, SKY_H, 64, 16, 4, 31);
                float lam = clampf(1.15f - dm / mr * 0.45f, 0, 1);
                if (blood) c = SH(R_FIRE, 9 + lam * 4 - (n > 0.55f ? 2.0f : 0), x, y);
                else c = SH(R_BONE, 9 + lam * 4.5f - (n > 0.55f ? 2.0f : 0), x, y);
            }
            float cl = fbm((float)x, y * 3.0f, SKY_W, SKY_H * 3, 8, 6, 5, blood ? 91 : 90);
            float band = 1.0f - fabsf(t - 0.45f) * 1.8f;
            if (cl * band > 0.36f) {
                float cs = 1.5f + (cl * band - 0.36f) * 14.0f;
                if (dm < mr * 3) cs += (1 - dm / (mr * 3)) * 4;
                c = SH(base_ramp, cs, x, y);
            }
            if (y >= SKY_HORIZON - hill) {
                c = pal_index(blood ? R_BLOOD : R_NIGHT, 0);
                if (hashf(x, y, 61) < 0.004f && y < SKY_HORIZON - 4) c = pal_index(R_FIRE, 13);
            }
            pset(im, x, y, c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* decoration & pickup sprites                                         */
/* ------------------------------------------------------------------ */
static void flame(Image *im, float cx, float by, float h, float w, int seed)
{
    for (int y = (int)(by - h); y <= (int)by; y++)
        for (int x = (int)(cx - w - 1); x <= (int)(cx + w + 1); x++) {
            float t = (by - (y + 0.5f)) / h; /* 0 bottom .. 1 tip */
            if (t < 0 || t > 1) continue;
            float wob = sinf(t * 5 + seed) * w * 0.25f * t;
            float hw = w * sinf(sqrtf(t) * PI) * (1.0f - t * 0.3f) + 0.3f;
            float d = fabsf(x + 0.5f - cx - wob) / hw;
            if (d > 1) continue;
            int ramp = d < 0.45f && t < 0.6f ? R_GOLD : R_FIRE;
            pset(im, x, y, pal_index(ramp, d < 0.7f ? 15 : 13 + (t < 0.5f)));
        }
}

static void spr_candle(Image *im, int frame)
{
    *im = img_new(32, 64);
    capsule(im, 16, 62, 16, 62, 7, 7, R_GOLD, 2, 8);
    capsule(im, 9, 63, 23, 63, 2, 2, R_GOLD, 2, 8);
    capsule(im, 16, 60, 16, 22, 1.6f, 1.2f, R_GOLD, 3, 11);
    ellipse(im, 16, 42, 3, 2, R_GOLD, 3, 11);
    for (int i = -1; i <= 1; i += 2) {
        for (float t = 0; t <= 1.0f; t += 0.05f) {
            float x = 16 + i * t * 10, y = 26 - sinf(t * PI * 0.5f) * 0 + t * t * -4;
            capsule(im, x, y, x, y, 1.3f, 1.3f, R_GOLD, 3, 11);
        }
    }
    for (int k = -1; k <= 1; k++) {
        float x = 16 + k * 10, top = k == 0 ? 10 : 14;
        float base = k == 0 ? 23 : 22;
        ellipse(im, x, base, 3, 1.2f, R_GOLD, 4, 12);
        for (int y = (int)top; y < (int)base; y++) {
            pset(im, (int)x - 1, y, pal_index(R_BONE, 12));
            pset(im, (int)x, y, pal_index(R_BONE, 10));
        }
        flame(im, x - 0.5f + ((frame + k) & 1 ? 0.4f : -0.4f), top, 6 + ((frame + k + 2) % 3) * 0.8f, 1.8f, frame * 3 + k);
    }
    outline(im, 2);
}

static void spr_brazier(Image *im, int frame)
{
    *im = img_new(40, 56);
    capsule(im, 20, 36, 6, 55, 1.5f, 1.5f, R_GRAY, 2, 6);
    capsule(im, 20, 36, 34, 55, 1.5f, 1.5f, R_GRAY, 2, 6);
    capsule(im, 20, 36, 20, 55, 1.5f, 1.5f, R_GRAY, 2, 6);
    for (int x = 5; x < 35; x++)
        for (int y = 28; y < 40; y++) {
            float dx = (x + 0.5f - 20) / 15, dy = (y + 0.5f - 29) / 10;
            if (dx * dx + dy * dy <= 1 && y >= 29) pset(im, x, y, SH(R_GRAY, 3 + (1 - fabsf(dx)) * 3 - dy * 2, x, y));
        }
    capsule(im, 5, 29, 35, 29, 1.6f, 1.6f, R_GRAY, 4, 9);
    for (int k = 0; k < 5; k++) {
        float fx = 9 + k * 5.5f + ((frame + k) % 3 - 1) * 0.8f;
        float fh = 14 + ((k * 7 + frame * 5) % 9) + (k == 2 ? 6 : 0);
        flame(im, fx, 30, fh, 3.2f, frame * 7 + k);
    }
    for (int k = 0; k < 4; k++)
        pset(im, 8 + (int)(hashf(k, frame, 3) * 24), 4 + (int)(hashf(k, frame, 4) * 10), pal_index(R_FIRE, 14));
    outline(im, 2);
}

static void spr_chandelier(Image *im, int frame)
{
    *im = img_new(64, 48);
    for (int y = 0; y < 24; y++) pset(im, 32, y, pal_index(R_GRAY, (y % 3) ? 5 : 2)), pset(im, 31, y, pal_index(R_GRAY, 3));
    for (int k = -1; k <= 1; k += 2) line_c(im, 32, 20, 32 + k * 26, 34, pal_index(R_GRAY, 3));
    for (float a = 0; a < TAU; a += 0.02f) {
        float x = 32 + cosf(a) * 26, y = 36 + sinf(a) * 5;
        capsule(im, x, y, x, y, 1.4f, 1.4f, R_GRAY, 2, 7);
    }
    for (int k = 0; k < 7; k++) {
        float a = PI * 0.08f + k * PI * 0.84f / 6;
        float x = 32 - cosf(a) * 26, y = 36 + sinf(a) * 5;
        for (int yy = (int)y - 6; yy < (int)y - 1; yy++) pset(im, (int)x, yy, pal_index(R_BONE, 11));
        flame(im, x + 0.5f, y - 6, 5 + ((frame + k) % 2) * 1.2f, 1.6f, k + frame);
    }
    outline(im, 2);
}

static void spr_pillar(Image *im)
{
    *im = img_new(32, 96);
    for (int x = 0; x < 32; x++)
        for (int y = 0; y < 96; y++) {
            float hw = 10;
            if (y < 12 || y >= 86) hw = 14.5f;
            else if (y < 16 || y >= 82) hw = 12;
            float ox = (x + 0.5f - 16) / hw;
            if (fabsf(ox) > 1) continue;
            float lam = clampf(ox * LX + sqrtf(1 - ox * ox) * 0.95f, 0, 1);
            float s = 2.5f + lam * 8 + (fbm((float)x, (float)y, 32, 96, 4, 12, 3, 9) - 0.5f) * 2;
            if (y >= 16 && y < 82 && (x % 4) == 0) s -= 1.5f;
            if (y == 12 || y == 16 || y == 82 || y == 85) s -= 2.5f;
            pset(im, x, y, SH(R_COOL, s, x, y));
        }
}

static void spr_grave(Image *im)
{
    *im = img_new(32, 34);
    for (int x = 3; x < 29; x++)
        for (int y = 2; y < 34; y++) {
            float dx = x + 0.5f - 16, dy = y + 0.5f - 13;
            if (y < 13 && dx * dx + dy * dy > 13 * 13) continue;
            float s = 6.5f + (fbm((float)x, (float)y, 32, 34, 4, 4, 3, 2) - 0.5f) * 3 - dx * 0.08f;
            if (x == 3 || (y < 13 && dx * dx + dy * dy > 11.8f * 11.8f && dx > 0)) s -= 2;
            int ramp = R_GRAY;
            if (y > 26 && hashf(x, y, 4) < (y - 26) / 8.0f) { ramp = R_MOSS; s = 3 + hashf(x, y, 5) * 3; }
            pset(im, x, y, SH(ramp, s, x, y));
        }
    for (int y = 7; y < 22; y++) shift_px(im, 16, y, -4);
    for (int x = 11; x < 22; x++) shift_px(im, x, 11, -4);
    for (int x = 9; x < 24; x += 2) shift_px(im, x, 25, -3);
    outline(im, 2);
}

static void spr_cross(Image *im)
{
    *im = img_new(32, 52);
    capsule(im, 16, 4, 16, 48, 2.8f, 3.4f, R_GRAY, 3, 9);
    capsule(im, 5, 15, 27, 15, 2.6f, 2.6f, R_GRAY, 3, 9);
    ring(im, 16, 15, 7, 8.6f, pal_index(R_GRAY, 6));
    for (int x = 9; x < 24; x++) for (int y = 47; y < 52; y++) pset(im, x, y, SH(R_GRAY, 4 + hashf(x, y, 1) * 2, x, y));
    for (int y = 34; y < 52; y++) if (hashf(16, y, 9) < 0.5f) pset(im, 15 + (hashf(y, 1, 9) < 0.5f), y, pal_index(R_MOSS, 4));
    outline(im, 2);
}

static void branch(Image *im, float x, float y, float ang, float len, float r, int depth, int seed)
{
    float x1 = x + cosf(ang) * len, y1 = y + sinf(ang) * len;
    capsule(im, x, y, x1, y1, r, r * 0.7f, R_WOOD, 1, 4);
    if (depth <= 0) return;
    int n = 2 + (hash3(depth, seed, 1) % 2);
    for (int i = 0; i < n; i++) {
        float da = (hashf(depth, seed + i, 2) - 0.5f) * 1.6f;
        branch(im, x1, y1, ang + da, len * (0.55f + hashf(i, seed, 3) * 0.25f), r * 0.65f, depth - 1, seed * 3 + i + 1);
    }
}

static void spr_tree(Image *im)
{
    *im = img_new(72, 104);
    capsule(im, 36, 103, 34, 60, 6, 4, R_WOOD, 1, 4);
    capsule(im, 36, 103, 26, 103, 3, 2, R_WOOD, 1, 3);
    capsule(im, 36, 103, 46, 102, 3, 2, R_WOOD, 1, 3);
    branch(im, 34, 62, -PI / 2 - 0.4f, 22, 3.6f, 3, 1);
    branch(im, 34, 62, -PI / 2 + 0.5f, 20, 3.2f, 3, 7);
    branch(im, 35, 78, -0.3f, 16, 2.5f, 2, 4);
    outline(im, 1);
}

static void skull(Image *im, float x, float y, float s, int sh)
{
    ellipse(im, x, y, 4.5f * s, 4.0f * s, R_BONE, sh - 6, sh);
    ellipse(im, x, y + 3.2f * s, 3.0f * s, 2.0f * s, R_BONE, sh - 7, sh - 2);
    ellipse_c(im, x - 1.7f * s, y + 0.5f * s, 1.2f * s, 1.3f * s, pal_index(R_GRAY, 0));
    ellipse_c(im, x + 1.7f * s, y + 0.5f * s, 1.2f * s, 1.3f * s, pal_index(R_GRAY, 0));
}

static void spr_skulls(Image *im)
{
    *im = img_new(32, 18);
    capsule(im, 3, 15, 29, 13, 1.5f, 1.5f, R_BONE, 4, 10);
    capsule(im, 5, 12, 25, 16, 1.3f, 1.3f, R_BONE, 4, 9);
    skull(im, 8, 11, 1.0f, 11);
    skull(im, 23, 12, 1.0f, 10);
    skull(im, 15.5f, 7, 1.1f, 12);
    outline(im, 2);
}

static void spr_coffin(Image *im)
{
    *im = img_new(32, 66);
    float pts[] = {10, 1, 22, 1, 29, 16, 24, 65, 8, 65, 3, 16};
    poly_grad(im, pts, 6, R_WOOD, 5, 2.5f, 8, 0, 26, 0, 1.0f);
    for (int y = 1; y < 65; y++) shift_px(im, 16, y, -1);
    capsule(im, 16, 12, 16, 40, 1.5f, 1.5f, R_GOLD, 5, 12);
    capsule(im, 9, 20, 23, 20, 1.5f, 1.5f, R_GOLD, 5, 12);
    outline(im, 2);
}

static void spr_cage(Image *im)
{
    *im = img_new(32, 72);
    for (int y = 0; y < 16; y++) pset(im, 16, y, pal_index(R_GRAY, (y % 3) ? 5 : 2));
    skull(im, 16, 30, 1.0f, 10);
    capsule(im, 16, 35, 16, 52, 1.0f, 1.0f, R_BONE, 4, 9);
    for (int k = 0; k < 3; k++) capsule(im, 11, 38 + k * 4, 21, 38 + k * 4, 0.8f, 0.8f, R_BONE, 4, 9);
    capsule(im, 11, 36, 9, 56, 0.9f, 0.9f, R_BONE, 4, 9);
    capsule(im, 21, 36, 23, 56, 0.9f, 0.9f, R_BONE, 4, 9);
    for (int k = 0; k < 7; k++) {
        float a = (k / 6.0f - 0.5f) * PI * 0.9f;
        float off = sinf(a) * 12;
        for (int y = 18; y < 66; y++) {
            float t = (y - 18) / 48.0f;
            float bulge = sinf(t * PI) * 0.8f + 0.2f;
            pset(im, (int)(16 + off * bulge), y, pal_index(R_GRAY, 3 + (k == 3) * 2));
        }
    }
    for (int x = 4; x < 29; x++) { pset(im, x, 18, pal_index(R_GRAY, 4)); pset(im, x, 66, pal_index(R_GRAY, 3)); }
}

static void spr_potion(Image *im)
{
    *im = img_new(14, 20);
    ellipse(im, 7, 13.5f, 6, 6, R_SPECTRE, 4, 9);
    for (int y = 3; y < 9; y++) for (int x = 5; x < 9; x++) pset(im, x, y, pal_index(R_SPECTRE, 6 + (x == 5) * 3));
    ellipse(im, 7, 14.5f, 4.8f, 4.6f, R_BLOOD, 6, 12);
    for (int x = 4; x < 10; x++) for (int y = 1; y < 4; y++) pset(im, x, y, pal_index(R_WOOD, 6));
    pset(im, 4, 11, pal_index(R_BONE, 15));
    pset(im, 5, 10, pal_index(R_BONE, 14));
    outline(im, 2);
}

static void spr_chalice(Image *im)
{
    *im = img_new(20, 24);
    for (int x = 2; x < 18; x++)
        for (int y = 2; y < 11; y++) {
            float dx = (x + 0.5f - 10) / 8, dy = (y + 0.5f - 2) / 9;
            if (dx * dx + dy * dy <= 1) pset(im, x, y, SH(R_GOLD, 5 + (1 - fabsf(dx)) * 7 - dx * 2, x, y));
        }
    for (int x = 3; x < 17; x++) pset(im, x, 2, pal_index(R_BLOOD, 9)), pset(im, x, 3, pal_index(R_BLOOD, 6));
    capsule(im, 10, 11, 10, 19, 1.4f, 1.4f, R_GOLD, 5, 12);
    ellipse(im, 10, 15, 2.4f, 1.6f, R_GOLD, 5, 12);
    ellipse(im, 10, 21, 6, 2, R_GOLD, 4, 12);
    pset(im, 10, 7, pal_index(R_CRIMSON, 15));
    pset(im, 9, 7, pal_index(R_CRIMSON, 14));
    outline(im, 2);
}

static void spr_bolts(Image *im)
{
    *im = img_new(26, 14);
    for (int k = 0; k < 4; k++) {
        float y = 3 + k * 2.5f;
        line_c(im, 3, y + 2, 19, y, pal_index(R_WOOD, 8));
        float tip[] = {19, y - 1.5f, 25, y - 0.5f, 19, y + 1.5f};
        poly_grad(im, tip, 3, R_BONE, 13, 9, 19, y - 2, 19, y + 2, 0);
        pset(im, 3, (int)y + 2, pal_index(R_BLOOD, 9));
        pset(im, 4, (int)y + 1, pal_index(R_BLOOD, 9));
    }
    capsule(im, 9, 3, 9, 12, 1.5f, 1.5f, R_WOOD, 2, 5);
    outline(im, 2);
}

static void spr_shot(Image *im)
{
    *im = img_new(22, 16);
    ellipse(im, 9, 9.5f, 7, 6, R_WOOD, 2, 7);
    capsule(im, 6, 4, 12, 4, 1.2f, 1.2f, R_WOOD, 1, 4);
    for (int k = 0; k < 5; k++) ellipse(im, 15 + (k % 3) * 2.2f, 13 - (k / 3) * 2.2f, 1.3f, 1.3f, R_GRAY, 3, 10);
    outline(im, 2);
}

static void spr_blunder_pick(Image *im)
{
    *im = img_new(52, 16);
    float stock[] = {1, 8, 18, 6, 22, 9, 18, 13, 3, 15};
    poly_grad(im, stock, 5, R_WOOD, 6, 3, 0, 6, 0, 15, 1);
    capsule(im, 18, 7.5f, 46, 6.5f, 2.0f, 2.4f, R_GOLD, 4, 11);
    ellipse(im, 47, 6.5f, 2.6f, 4.5f, R_GOLD, 4, 12);
    ellipse_c(im, 48, 6.5f, 1.2f, 2.6f, pal_index(R_GRAY, 0));
    capsule(im, 20, 10, 30, 10, 1.2f, 1.2f, R_WOOD, 3, 7);
    pset(im, 21, 4, pal_index(R_GRAY, 6));
    pset(im, 22, 3, pal_index(R_GRAY, 7));
    outline(im, 2);
}

static void spr_key(Image *im, int metal, int gem)
{
    *im = img_new(22, 12);
    ring(im, 5.5f, 6, 2.2f, 4.6f, pal_index(metal, 9));
    ring(im, 5.5f, 6, 3.6f, 4.6f, pal_index(metal, 6));
    capsule(im, 9, 6, 20, 6, 1.1f, 1.1f, metal, 5, 12);
    for (int x = 15; x < 21; x += 2) for (int y = 7; y < 10; y++) pset(im, x, y, pal_index(metal, 7));
    ellipse_c(im, 5.5f, 6, 1.5f, 1.5f, pal_index(gem, 15));
    outline(im, 2);
}

/* ------------------------------------------------------------------ */
/* monsters                                                            */
/* ------------------------------------------------------------------ */
typedef struct { float walk, raise, lean, drop, mouth; } GPose;

static void claws(Image *im, float x, float y, float dir)
{
    for (int k = -1; k <= 1; k++) line_c(im, x, y, x + dir * (2 + k * 1.5f) + k, y + 4 - fabsf((float)k), pal_index(R_BONE, 12));
}

static void draw_ghoul(Image *im, GPose p)
{
    *im = img_new(64, 72);
    float cx = 32, gy = 71;
    float hip = 46 + p.drop;
    for (int side = -1; side <= 1; side += 2) {
        float st = p.walk * side;
        float hx = cx + side * 5, kx = cx + side * 6 + st * 3, ky = minf(gy - 4, hip + 12);
        float fx = cx + side * 7 + st * 5;
        capsule(im, hx, hip, kx, ky, 3.6f, 3.0f, R_GHOUL, 2, 8);
        capsule(im, kx, ky, fx, gy - 2, 3.0f, 2.4f, R_GHOUL, 2, 8);
        ellipse(im, fx + side, gy - 1.5f, 3.6f, 1.6f, R_GHOUL, 1, 5);
    }
    float tx = cx + p.lean * 4, ty = hip - 12;
    float cloth[] = {cx - 9, hip - 3, cx + 9, hip - 3, cx + 7, hip + 8, cx + 2, hip + 5, cx - 1, hip + 10, cx - 6, hip + 6};
    poly_grad(im, cloth, 6, R_WOOD, 4, 1.5f, cx, hip - 3, cx, hip + 10, 1.5f);
    ellipse(im, tx, ty, 11, 13, R_GHOUL, 2, 10);
    for (int i = 0; i < 3; i++)
        for (int x = -7; x <= 7; x++) shift_px(im, (int)(tx + x), (int)(ty + 1 + i * 3.5f + x * x / 30.0f), -3);
    for (int y = (int)ty - 10; y < (int)ty + 10; y += 2) shift_px(im, (int)tx, y, -2);
    float hx = cx + p.lean * 8, hy = ty - 17 + p.drop * 0.15f;
    capsule(im, tx + p.lean * 2, ty - 10, hx, hy + 4, 3.2f, 2.6f, R_GHOUL, 2, 8);
    ellipse(im, hx, hy, 7.5f, 8, R_GHOUL, 2, 10);
    for (int x = -5; x <= 5; x++) shift_px(im, (int)(hx + x), (int)(hy - 3), -2);
    pset(im, (int)hx - 3, (int)hy - 1, pal_index(R_FIRE, 15));
    pset(im, (int)hx - 2, (int)hy - 1, pal_index(R_FIRE, 14));
    pset(im, (int)hx + 2, (int)hy - 1, pal_index(R_FIRE, 14));
    pset(im, (int)hx + 3, (int)hy - 1, pal_index(R_FIRE, 15));
    float mh = 1.3f + p.mouth * 2.0f;
    ellipse_c(im, hx, hy + 4, 3.6f, mh, pal_index(R_BLOOD, 1));
    for (int x = -3; x <= 3; x += 2) pset(im, (int)hx + x, (int)(hy + 4 - mh + 1), pal_index(R_BONE, 12));
    for (int side = -1; side <= 1; side += 2) {
        float sx = tx + side * 10, sy = ty - 7;
        float ex = lerpf(tx + side * 15, tx + side * 16, p.raise);
        float ey = lerpf(ty + 3 - side * p.walk * 2, ty - 14, p.raise);
        float hx2 = lerpf(tx + side * 14 + p.lean * 3, tx + side * 13, p.raise);
        float hy2 = lerpf(ty + 15 + side * p.walk * 3, ty - 26, p.raise);
        capsule(im, sx, sy, ex, ey, 3.2f, 2.6f, R_GHOUL, 2, 9);
        capsule(im, ex, ey, hx2, hy2, 2.6f, 2.0f, R_GHOUL, 2, 9);
        claws(im, hx2, hy2 + (p.raise > 0.5f ? -4 : 1), (float)side);
    }
    /* blood smears */
    for (int k = 0; k < 6; k++)
        pset(im, (int)(tx - 6 + hashf(k, 1, 4) * 12), (int)(ty + hashf(k, 2, 4) * 10 - 4), pal_index(R_BLOOD, 4));
    outline(im, 3);
}

static void draw_ghoul_dead(Image *im)
{
    *im = img_new(64, 72);
    ellipse(im, 30, 68, 26, 3.5f, R_BLOOD, 1, 4);
    ellipse(im, 28, 65, 15, 4.5f, R_GHOUL, 2, 8);
    ellipse(im, 46, 64, 5.5f, 4.5f, R_GHOUL, 2, 8);
    capsule(im, 14, 66, 4, 68, 2.5f, 2, R_GHOUL, 2, 7);
    capsule(im, 16, 64, 6, 63, 2.5f, 2, R_GHOUL, 2, 7);
    capsule(im, 36, 62, 44, 58, 2.2f, 1.8f, R_GHOUL, 2, 7);
    ellipse_c(im, 47, 66, 2, 1, pal_index(R_BLOOD, 1));
    for (int k = 0; k < 10; k++) pset(im, 10 + (int)(hashf(k, 0, 2) * 40), 66 + (int)(hashf(k, 1, 2) * 5), pal_index(R_BLOOD, 6));
    outline(im, 3);
}

typedef struct { float walk, arm, drop, lean; int armpose; bool scatter; } SPose;

static void draw_skeleton(Image *im, SPose p)
{
    *im = img_new(64, 76);
    float cx = 32, gy = 75;
    float hip = 47 + p.drop;
    for (int side = -1; side <= 1; side += 2) {
        float st = p.walk * side;
        float kx = cx + side * 5 + st * 3, ky = minf(gy - 6, hip + 14);
        float fx = cx + side * 6 + st * 5;
        capsule(im, cx + side * 4, hip, kx, ky, 1.9f, 1.6f, R_BONE, 5, 12);
        capsule(im, kx, ky, fx, gy - 2, 1.6f, 1.4f, R_BONE, 5, 12);
        ellipse(im, kx, ky, 2.3f, 2.3f, R_BONE, 5, 12);
        capsule(im, fx, gy - 1, fx + side * 3, gy - 1, 1.3f, 1.1f, R_BONE, 4, 10);
    }
    ellipse(im, cx, hip, 7, 3.6f, R_BONE, 4, 11);
    ellipse_c(im, cx, hip + 0.5f, 2.4f, 1.5f, TRANSP);
    float lean = p.lean;
    for (int k = 0; k < 8; k++) {
        float y = hip - 2 - k * 3;
        ellipse(im, cx + lean * (hip - y) / 10, y, 1.7f, 1.4f, R_BONE, 5, 11);
    }
    float chx = cx + lean * 2.2f, chy = hip - 18;
    for (int i = 0; i < 4; i++) {
        float rx = 9 - i;
        for (float x = -rx; x <= rx; x += 0.5f) {
            float y = chy - 4 + i * 3.6f + (x / rx) * (x / rx) * 3.5f;
            pset(im, (int)(chx + x), (int)y, pal_index(R_BONE, 11));
            pset(im, (int)(chx + x), (int)y + 1, pal_index(R_BONE, 6));
        }
    }
    float shy = hip - 24;
    capsule(im, chx - 10, shy, chx + 10, shy, 1.3f, 1.3f, R_BONE, 5, 12);
    /* left arm hangs */
    capsule(im, chx - 10, shy, chx - 12, shy + 11 - p.walk, 1.6f, 1.4f, R_BONE, 5, 12);
    capsule(im, chx - 12, shy + 11 - p.walk, chx - 11, shy + 21, 1.4f, 1.2f, R_BONE, 5, 12);
    float ex, ey, hx, hy;
    if (p.armpose == 1) { ex = chx + 14; ey = shy - 6; hx = chx + 10; hy = shy - 16; }
    else if (p.armpose == 2) { ex = chx + 9; ey = shy + 2; hx = chx + 3; hy = shy + 3; }
    else { ex = chx + 12; ey = shy + 11 + p.walk; hx = chx + 11; hy = shy + 21; }
    capsule(im, chx + 10, shy, ex, ey, 1.6f, 1.4f, R_BONE, 5, 12);
    capsule(im, ex, ey, hx, hy, 1.4f, 1.2f, R_BONE, 5, 12);
    if (p.armpose == 1) {
        ellipse_c(im, hx, hy - 3, 4.5f, 4.5f, pal_index(R_FIRE, 13));
        ellipse_c(im, hx, hy - 3, 2.8f, 2.8f, pal_index(R_FIRE, 15));
        ellipse_c(im, hx, hy - 3, 1.4f, 1.4f, pal_index(R_GOLD, 15));
    }
    float sx = chx + lean * 3, sy = shy - 8;
    ellipse(im, sx, sy, 6.4f, 7, R_BONE, 4, 12);
    ellipse(im, sx, sy + 6, 4.4f, 2.6f, R_BONE, 4, 10);
    ellipse_c(im, sx - 2.7f, sy, 1.9f, 2.1f, pal_index(R_GRAY, 0));
    ellipse_c(im, sx + 2.7f, sy, 1.9f, 2.1f, pal_index(R_GRAY, 0));
    pset(im, (int)(sx - 2.7f), (int)sy, pal_index(R_BLOOD, 14));
    pset(im, (int)(sx + 2.7f), (int)sy, pal_index(R_BLOOD, 14));
    pset(im, (int)sx, (int)sy + 3, pal_index(R_GRAY, 0));
    for (int x = -3; x <= 3; x += 2) pset(im, (int)sx + x, (int)sy + 6, pal_index(R_BONE, 3));
    if (p.scatter)
        for (int x = 0; x < im->w; x++)
            for (int y = 0; y < im->h; y++)
                if (hashf(x / 3, y / 3, 5) < 0.3f) pset(im, x, y, TRANSP);
    outline(im, 3);
}

static void draw_skeleton_dead(Image *im)
{
    *im = img_new(64, 76);
    for (int k = 0; k < 7; k++) {
        float x = 12 + hashf(k, 0, 8) * 40, y = 68 + hashf(k, 1, 8) * 6;
        float a = (hashf(k, 2, 8) - 0.5f) * 1.2f, l = 5 + hashf(k, 3, 8) * 6;
        capsule(im, x - cosf(a) * l, y - sinf(a) * l, x + cosf(a) * l, y + sinf(a) * l, 1.5f, 1.5f, R_BONE, 4, 11);
    }
    for (int i = 0; i < 3; i++)
        for (float x = -7; x <= 7; x += 0.5f) pset(im, (int)(30 + x), (int)(66 + i * 2.5f + x * x / 20), pal_index(R_BONE, 10));
    skull(im, 42, 68, 1.2f, 12);
    outline(im, 3);
}

typedef struct { float phase, arms, fade; float shrink; } WPose;
typedef struct { float cx, phase; } RobeCtx;

static u8 robe_fn(int x, int y, const void *c)
{
    const RobeCtx *r = c;
    float s = 4.5f - fabsf(x + 0.5f - r->cx) * 0.18f + sinf(x * 0.55f + r->phase) * 1.4f - y * 0.02f;
    return SH(R_PURPLE, s, x, y);
}

static void draw_wraith(Image *im, WPose p)
{
    *im = img_new(64, 88);
    float cx = 32;
    float sc = 1.0f - p.shrink;
    float pts[80];
    int n = 0;
    float bot = 6 + 74 * sc;
    pts[n++] = cx; pts[n++] = 4;
    pts[n++] = cx + 9; pts[n++] = 10;
    pts[n++] = cx + 14; pts[n++] = 24;
    pts[n++] = cx + 17; pts[n++] = 6 + 44 * sc;
    pts[n++] = cx + 20; pts[n++] = bot;
    for (int i = 0; i <= 10; i++) {
        float x = cx + 20 - i * 4;
        float y = bot + sinf(p.phase + i * 1.3f) * 3 + (i % 2) * 5;
        pts[n++] = x; pts[n++] = y;
    }
    pts[n++] = cx - 17; pts[n++] = 6 + 44 * sc;
    pts[n++] = cx - 14; pts[n++] = 24;
    pts[n++] = cx - 9; pts[n++] = 10;
    RobeCtx rc = {cx, p.phase};
    poly(im, pts, n / 2, robe_fn, &rc);
    ellipse_c(im, cx, 15, 6, 7, pal_index(R_GRAY, 0));
    ellipse_c(im, cx, 16, 4.5f, 5, pal_index(R_NIGHT, 0));
    u8 eye = pal_index(R_SPECTRE, 15);
    pset(im, (int)cx - 3, 14, eye); pset(im, (int)cx - 2, 14, eye);
    pset(im, (int)cx + 2, 14, eye); pset(im, (int)cx + 3, 14, eye);
    pset(im, (int)cx - 3, 15, pal_index(R_SPECTRE, 13));
    pset(im, (int)cx + 3, 15, pal_index(R_SPECTRE, 13));
    for (int side = -1; side <= 1; side += 2) {
        float sx = cx + side * 12, sy = 24;
        float hx = lerpf(cx + side * 20, cx + side * 10, p.arms);
        float hy = lerpf(46 + sinf(p.phase) * 2, 30, p.arms);
        float sl[] = {sx - 3, sy, sx + 3, sy + 2, hx + 3, hy, hx - 3, hy};
        poly_grad(im, sl, 4, R_PURPLE, 2, 5, sx, sy, hx, hy, 1);
        for (int k = -1; k <= 1; k++) line_c(im, hx, hy, hx + k * 2 + side, hy + 5, pal_index(R_BONE, 11));
    }
    if (p.arms > 0.5f) {
        ellipse_c(im, cx, 32, 6, 6, pal_index(R_PURPLE, 13));
        ellipse_c(im, cx, 32, 4, 4, pal_index(R_SPECTRE, 14));
        ellipse_c(im, cx, 32, 2, 2, pal_index(R_SPECTRE, 15));
    }
    outline(im, 2);
    if (p.fade > 0)
        for (int x = 0; x < im->w; x++)
            for (int y = 0; y < im->h; y++)
                if (hashf(x, y, 12) < p.fade) pset(im, x, y, TRANSP);
}

static void draw_bat(Image *im, int frame)
{
    *im = img_new(40, 28);
    float cx = 20, cy = 13;
    for (int side = -1; side <= 1; side += 2) {
        float tipy = frame == 0 ? 2 : 24;
        float midy = frame == 0 ? 6 : 18;
        float w[] = {cx, cy - 2, cx + side * 9, midy, cx + side * 19, tipy, cx + side * 15, cy + 2,
                     cx + side * 11, cy + 5, cx + side * 6, cy + 3, cx, cy + 4};
        poly_grad(im, w, 7, R_PURPLE, 1, 4, cx, cy, cx + side * 18, tipy, 0.8f);
        line_c(im, cx, cy - 1, cx + side * 19, tipy, pal_index(R_GRAY, 5));
        line_c(im, cx + side * 9, midy, cx + side * 15, cy + 2, pal_index(R_GRAY, 4));
    }
    ellipse(im, cx, cy + 2, 4, 5, R_WOOD, 1, 6);
    ellipse(im, cx, cy - 3, 3.2f, 3, R_WOOD, 1, 6);
    pset(im, (int)cx - 2, (int)cy - 7, pal_index(R_WOOD, 4));
    pset(im, (int)cx + 2, (int)cy - 7, pal_index(R_WOOD, 4));
    pset(im, (int)cx - 1, (int)cy - 3, pal_index(R_BLOOD, 15));
    pset(im, (int)cx + 1, (int)cy - 3, pal_index(R_BLOOD, 15));
    pset(im, (int)cx, (int)cy - 1, pal_index(R_BONE, 13));
    outline(im, 2);
}

static void draw_bat_dead(Image *im)
{
    *im = img_new(40, 28);
    float w[] = {8, 26, 20, 21, 32, 26, 26, 27, 14, 27};
    poly_grad(im, w, 5, R_PURPLE, 1, 3, 8, 26, 32, 26, 0.6f);
    ellipse(im, 20, 25, 4, 2.5f, R_WOOD, 1, 5);
    ellipse_c(im, 23, 27, 4, 1, pal_index(R_BLOOD, 3));
    outline(im, 2);
}

typedef struct { float walk, raise, drop; bool mouth; } DPose;

static void draw_demon(Image *im, DPose p)
{
    *im = img_new(112, 136);
    float cx = 56, gy = 135, d = p.drop;
    for (int side = -1; side <= 1; side += 2) {
        float wy = 40 + d;
        float w[] = {cx + side * 14, wy, cx + side * 50, wy - 32, cx + side * 54, wy - 10, cx + side * 46, wy - 6,
                     cx + side * 51, wy + 12, cx + side * 38, wy + 12, cx + side * 40, wy + 30, cx + side * 20, wy + 22};
        poly_grad(im, w, 8, R_PURPLE, 1, 3.5f, cx, wy, cx + side * 50, wy - 30, 1);
        capsule(im, cx + side * 14, wy, cx + side * 50, wy - 32, 2.2f, 1.2f, R_BONE, 2, 6);
        capsule(im, cx + side * 50, wy - 32, cx + side * 51, wy + 12, 1.4f, 0.8f, R_BONE, 2, 5);
        capsule(im, cx + side * 50, wy - 32, cx + side * 40, wy + 30, 1.4f, 0.8f, R_BONE, 2, 5);
    }
    float hip = 88 + d;
    for (int side = -1; side <= 1; side += 2) {
        float st = p.walk * side;
        float kx = cx + side * 17 + st * 4, ky = minf(gy - 20, hip + 18);
        float ax = cx + side * 12 + st * 6, ay = minf(gy - 8, ky + 16);
        capsule(im, cx + side * 11, hip, kx, ky, 8, 6, R_BLOOD, 2, 8);
        capsule(im, kx, ky, ax, ay, 5.5f, 3.8f, R_BLOOD, 2, 8);
        capsule(im, ax, ay, ax + side * 1, gy - 4, 3.8f, 3.4f, R_BLOOD, 2, 7);
        ellipse(im, ax + side * 1, gy - 2.5f, 5, 2.8f, R_GRAY, 1, 4);
    }
    float belt[] = {cx - 22, hip - 8, cx + 22, hip - 8, cx + 18, hip + 6, cx, hip + 12, cx - 18, hip + 6};
    poly_grad(im, belt, 5, R_GRAY, 4, 1, cx, hip - 8, cx, hip + 12, 1);
    float ty = 64 + d;
    ellipse(im, cx, ty, 24, 24, R_BLOOD, 2, 9);
    ellipse(im, cx - 9, ty - 9, 10, 7, R_BLOOD, 3, 10);
    ellipse(im, cx + 9, ty - 9, 10, 7, R_BLOOD, 3, 10);
    for (int r = 0; r < 3; r++)
        for (int side = -1; side <= 1; side += 2) ellipse(im, cx + side * 4.5f, ty + 4 + r * 5.5f, 4, 2.6f, R_BLOOD, 3, 9);
    for (int side = -1; side <= 1; side += 2) {
        float sx = cx + side * 24, sy = ty - 14;
        ellipse(im, sx, sy, 9, 8, R_BLOOD, 3, 10);
        float ex = lerpf(cx + side * 34, cx + side * 32, p.raise), ey = lerpf(ty + 8, ty - 28, p.raise);
        float hx = lerpf(cx + side * 32, cx + side * 18, p.raise), hy = lerpf(ty + 28, ty - 44, p.raise);
        capsule(im, sx, sy, ex, ey, 6.5f, 5, R_BLOOD, 2, 9);
        capsule(im, ex, ey, hx, hy, 5, 4, R_BLOOD, 2, 9);
        for (int k = -1; k <= 1; k++)
            capsule(im, hx, hy, hx + k * 3 + side * 2, hy + (p.raise > 0.5f ? -6 : 6), 1.2f, 0.6f, R_BONE, 6, 12);
    }
    float hx = cx, hy = 28 + d;
    for (int side = -1; side <= 1; side += 2) {
        capsule(im, hx + side * 8, hy - 6, hx + side * 18, hy - 14, 4, 3, R_BONE, 3, 11);
        capsule(im, hx + side * 18, hy - 14, hx + side * 23, hy - 24, 3, 2, R_BONE, 3, 11);
        capsule(im, hx + side * 23, hy - 24, hx + side * 20, hy - 27, 2, 0.8f, R_BONE, 3, 11);
    }
    ellipse(im, hx, hy, 11, 12, R_BLOOD, 2, 10);
    ellipse(im, hx, hy + 8, 8, 5, R_BLOOD, 2, 8);
    for (int x = -7; x <= 7; x++) shift_px(im, (int)hx + x, (int)hy - 3 + abs(x) / 3, -3);
    for (int side = -1; side <= 1; side += 2)
        for (int k = 0; k < 4; k++) pset(im, (int)hx + side * (2 + k), (int)hy - 1 + k / 2, pal_index(R_GOLD, 15 - (k == 3)));
    ellipse_c(im, hx, hy + 8, 5, p.mouth ? 3.5f : 1.5f, pal_index(p.mouth ? R_FIRE : R_GRAY, p.mouth ? 13 : 0));
    pset(im, (int)hx - 3, (int)hy + 6, pal_index(R_BONE, 14));
    pset(im, (int)hx + 3, (int)hy + 6, pal_index(R_BONE, 14));
    if (p.raise > 0.5f) {
        ellipse_c(im, cx, ty - 50, 10, 10, pal_index(R_FIRE, 13));
        ellipse_c(im, cx, ty - 50, 7, 7, pal_index(R_FIRE, 15));
        ellipse_c(im, cx, ty - 50, 3.5f, 3.5f, pal_index(R_GOLD, 15));
    }
    outline(im, 3);
}

static void draw_demon_dead(Image *im)
{
    *im = img_new(112, 136);
    ellipse(im, 56, 129, 50, 6, R_BLOOD, 1, 4);
    float w[] = {20, 128, 50, 108, 70, 126};
    poly_grad(im, w, 3, R_PURPLE, 1, 3, 20, 128, 70, 108, 1);
    ellipse(im, 52, 124, 26, 9, R_BLOOD, 2, 8);
    ellipse(im, 84, 122, 10, 8, R_BLOOD, 2, 9);
    capsule(im, 88, 116, 100, 104, 3, 1.5f, R_BONE, 3, 11);
    capsule(im, 26, 126, 8, 130, 5, 4, R_BLOOD, 2, 7);
    capsule(im, 64, 120, 70, 108, 4, 3, R_BLOOD, 2, 7);
    outline(im, 3);
}

static void orb(Image *im, int size, int outer, int mid, int core, int frame)
{
    *im = img_new(size, size);
    float c = size / 2.0f, r = size / 2.0f - 1;
    for (int x = 0; x < size; x++)
        for (int y = 0; y < size; y++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c;
            float a = atan2f(dy, dx);
            float rr = r * (0.8f + 0.2f * sinf(a * 5 + frame * 2.1f));
            float d = sqrtf(dx * dx + dy * dy) / rr;
            if (d > 1) continue;
            u8 col = d < 0.35f ? pal_index(core, 15) : d < 0.7f ? pal_index(mid, 15) : pal_index(outer, 13 + (d < 0.85f));
            pset(im, x, y, col);
        }
}

static void explosion(Image *im, int frame)
{
    *im = img_new(40, 40);
    float r = 8 + frame * 6;
    for (int x = 0; x < 40; x++)
        for (int y = 0; y < 40; y++) {
            float dx = x + 0.5f - 20, dy = y + 0.5f - 20;
            float d = sqrtf(dx * dx + dy * dy);
            float n = hashf(x / 2, y / 2, frame + 40);
            if (d > r * (0.7f + n * 0.4f)) continue;
            float t = d / r;
            u8 c;
            if (frame == 2) c = t > 0.5f ? pal_index(R_GRAY, 3 + (int)(n * 3)) : pal_index(R_FIRE, 13);
            else c = t < 0.4f - frame * 0.15f ? pal_index(R_GOLD, 15) : t < 0.75f ? pal_index(R_FIRE, 15) : pal_index(R_FIRE, 13);
            pset(im, x, y, c);
        }
}

/* ------------------------------------------------------------------ */
/* first-person weapons                                                */
/* ------------------------------------------------------------------ */
static void glove(Image *im, float x, float y, float rx, float ry)
{
    ellipse(im, x, y, rx, ry, R_WOOD, 1, 5);
    for (int k = 0; k < 4; k++) shift_px(im, (int)(x - rx * 0.5f + k * rx * 0.33f), (int)(y - ry * 0.4f), -2);
}

static void draw_dagger(Image *im, float hx, float hy, float ang)
{
    *im = img_new(160, 130);
    float dx = cosf(ang), dy = sinf(ang), px = -dy, py = dx;
    float tipx = hx + dx * 74, tipy = hy + dy * 74;
    float blade[] = {hx + px * 5, hy + py * 5, hx + dx * 50 + px * 4, hy + dy * 50 + py * 4, tipx, tipy,
                     hx + dx * 50 - px * 3.5f, hy + dy * 50 - py * 3.5f, hx - px * 5, hy - py * 5};
    poly_grad(im, blade, 5, R_COOL, 6, 13, hx - px * 5, hy - py * 5, hx + px * 5, hy + py * 5, 0.8f);
    line_c(im, hx + dx * 4, hy + dy * 4, hx + dx * 46, hy + dy * 46, pal_index(R_COOL, 4));
    line_c(im, hx + px * 5, hy + py * 5, tipx, tipy, pal_index(R_BONE, 14));
    capsule(im, hx - px * 15, hy - py * 15, hx + px * 15, hy + py * 15, 3, 3, R_GOLD, 3, 12);
    ellipse(im, hx, hy, 4, 4, R_CRIMSON, 6, 12);
    capsule(im, hx, hy, hx - dx * 20, hy - dy * 20, 4, 4, R_WOOD, 1, 4);
    ellipse(im, hx - dx * 23, hy - dy * 23, 5, 5, R_GOLD, 3, 12);
    glove(im, hx - dx * 11 + 4, hy - dy * 11 + 6, 15, 12);
    outline(im, 2);
}

static void draw_crossbow(Image *im, bool loaded)
{
    *im = img_new(220, 130);
    float cx = 110;
    float stock[] = {86, 130, 134, 130, 120, 40, 100, 40};
    poly_grad(im, stock, 4, R_WOOD, 6.5f, 3.5f, 0, 40, 0, 130, 1.2f);
    for (int y = 42; y < 130; y++) pset(im, (int)cx, y, pal_index(R_WOOD, 1)), pset(im, (int)cx + 1, y, pal_index(R_WOOD, 2));
    for (int b = 0; b < 2; b++) {
        int y0 = b ? 98 : 62;
        float w0 = 14 + (y0 - 40) * 0.26f;
        float band[] = {cx - w0, (float)y0, cx + w0, (float)y0, cx + w0 + 1, y0 + 5.0f, cx - w0 - 1, y0 + 5.0f};
        poly_grad(im, band, 4, R_COOL, 10, 5, 0, (float)y0, 0, y0 + 5.0f, 0.5f);
    }
    float px = 0, py = 0;
    for (int i = 0; i <= 40; i++) {
        float t = i / 20.0f - 1.0f;
        float x = cx + t * 100, y = 40 + powf(fabsf(t), 1.5f) * 14;
        float r = 6.0f * (1 - fabsf(t) * 0.55f);
        if (i > 0) capsule(im, px, py, x, y, r, r, R_COOL, 2, 9);
        px = x; py = y;
    }
    ellipse(im, cx, 42, 12, 7, R_COOL, 3, 10);
    float nuty = loaded ? 88 : 44;
    line_c(im, 10, 54, cx, nuty, pal_index(R_BONE, 9));
    line_c(im, 210, 54, cx, nuty, pal_index(R_BONE, 9));
    line_c(im, 11, 55, cx, nuty + 1, pal_index(R_BONE, 5));
    line_c(im, 209, 55, cx, nuty + 1, pal_index(R_BONE, 5));
    if (loaded) {
        capsule(im, cx + 0.5f, 88, cx + 0.5f, 30, 1.8f, 1.6f, R_WOOD, 6, 11);
        float tip[] = {cx - 4.5f, 32, cx + 5.5f, 32, cx + 0.5f, 16};
        poly_grad(im, tip, 3, R_COOL, 13, 8, cx - 4, 0, cx + 5, 0, 0.5f);
        float f1[] = {cx - 6, 86, cx, 78, cx, 88};
        float f2[] = {cx + 7, 86, cx + 1, 78, cx + 1, 88};
        poly_grad(im, f1, 3, R_BLOOD, 8, 5, cx - 6, 80, cx, 88, 0);
        poly_grad(im, f2, 3, R_BLOOD, 8, 5, cx + 7, 80, cx, 88, 0);
    }
    glove(im, 80, 116, 20, 15);
    for (int k = 0; k < 4; k++) capsule(im, 86, 104 + k * 5, 104, 101 + k * 5, 3, 3, R_WOOD, 1, 5);
    glove(im, 148, 124, 18, 12);
    outline(im, 2);
}

typedef struct { float cx0, cx1, y0, y1, w0, w1; bool glow; } BarrelCtx;

static u8 barrel_fn(int x, int y, const void *c)
{
    const BarrelCtx *b = c;
    float t = clampf((y + 0.5f - b->y0) / (b->y1 - b->y0), 0, 1);
    float cx = lerpf(b->cx0, b->cx1, t), w = lerpf(b->w0, b->w1, t);
    float ox = clampf((x + 0.5f - cx) / w, -1, 1);
    float lam = clampf(ox * LX + sqrtf(1 - ox * ox) * 0.95f, 0, 1);
    float s = 2.5f + lam * 10;
    if (((int)y % 30) < 3) s -= 3;
    return SH(R_GOLD, s, x, y);
}

static void draw_blunderbuss(Image *im, bool hot)
{
    *im = img_new(220, 130);
    float stock[] = {78, 130, 152, 130, 140, 96, 92, 96};
    poly_grad(im, stock, 4, R_WOOD, 6, 2.5f, 0, 96, 0, 130, 1.2f);
    BarrelCtx bc = {113, 110, 124, 34, 21, 10, false};
    float barrel[] = {92, 124, 134, 124, 120, 34, 100, 34};
    poly(im, barrel, 4, barrel_fn, &bc);
    ellipse(im, 110, 30, 22, 9.5f, R_GOLD, 3, 13);
    ellipse_c(im, 110, 30.5f, 16, 6, pal_index(hot ? R_FIRE : R_GRAY, hot ? 14 : 0));
    ellipse_c(im, 110, 31, 11, 4, pal_index(hot ? R_GOLD : R_GRAY, hot ? 15 : 0));
    float lock[] = {128, 100, 142, 96, 146, 104, 134, 110};
    poly_grad(im, lock, 4, R_GRAY, 8, 3, 128, 96, 146, 110, 0.6f);
    capsule(im, 140, 96, 144, 86, 2, 1.5f, R_GRAY, 3, 9);
    glove(im, 90, 118, 20, 14);
    for (int k = 0; k < 4; k++) capsule(im, 94, 106 + k * 5, 108, 104 + k * 5, 3, 3, R_WOOD, 1, 5);
    glove(im, 150, 124, 18, 12);
    outline(im, 2);
}

static void draw_flash(Image *im)
{
    *im = img_new(120, 90);
    for (int k = 0; k < 11; k++) {
        float a = k * TAU / 11 + hashf(k, 0, 3) * 0.4f;
        float len = 24 + hashf(k, 1, 3) * 22;
        float w = 5 + hashf(k, 2, 3) * 3;
        float pts[] = {60 + cosf(a + 1.57f) * w, 45 + sinf(a + 1.57f) * w * 0.75f,
                       60 + cosf(a) * len, 45 + sinf(a) * len * 0.75f,
                       60 + cosf(a - 1.57f) * w, 45 + sinf(a - 1.57f) * w * 0.75f};
        Grad g = {R_FIRE, 15, 13, 60, 45, 60 + cosf(a) * len, 45 + sinf(a) * len * 0.75f, 0, 1};
        poly(im, pts, 3, grad_fn, &g);
    }
    ellipse_c(im, 60, 45, 16, 12, pal_index(R_FIRE, 15));
    ellipse_c(im, 60, 45, 9, 7, pal_index(R_GOLD, 15));
}

/* ------------------------------------------------------------------ */
/* font                                                                */
/* ------------------------------------------------------------------ */
static const u8 FONT5x7[64][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x08, 0x2A, 0x1C, 0x2A, 0x08}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x00, 0x08, 0x14, 0x22, 0x41}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x41, 0x22, 0x14, 0x08, 0x00}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x01, 0x01},
    {0x3E, 0x41, 0x41, 0x51, 0x32}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x7F, 0x20, 0x18, 0x20, 0x7F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x00, 0x7F, 0x41, 0x41},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x41, 0x41, 0x7F, 0x00, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40},
};

void font_draw(u8 *fb, int x, int y, const char *s, int scale, u8 col)
{
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 95) c = '?';
        const u8 *g = FONT5x7[c - 32];
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++) {
                if (!(g[cx] >> cy & 1)) continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + cx * scale + sx, py = y + cy * scale + sy;
                        if ((unsigned)px < SCR_W && (unsigned)py < SCR_H) fb[py * SCR_W + px] = col;
                    }
            }
        x += 6 * scale;
    }
}

void font_draw_shadow(u8 *fb, int x, int y, const char *s, int scale, u8 col)
{
    font_draw(fb, x + scale, y + scale, s, scale, 0);
    font_draw(fb, x, y, s, scale, col);
}

int font_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */
void art_init(void)
{
    build_palette();
    build_tables();

    for (int i = 0; i < WT_COUNT; i++) g_walls[i] = img_new(TW, TH);
    tex_stone(&g_walls[WT_STONE], R_COOL, 11, 6.5f, 1.0f);
    tex_stone(&g_walls[WT_STONE_WARM], R_WARM, 23, 6.5f, 1.0f);
    tex_window(&g_walls[WT_WINDOW]);
    tex_wood(&g_walls[WT_WOOD]);
    tex_moss(&g_walls[WT_MOSS]);
    tex_blood(&g_walls[WT_BLOOD]);
    tex_bones(&g_walls[WT_BONES]);
    tex_banner(&g_walls[WT_BANNER]);
    tex_organ(&g_walls[WT_ORGAN]);
    tex_door(&g_walls[WT_DOOR], R_GRAY, -1);
    tex_door(&g_walls[WT_DOOR_RED], R_BLOOD, R_BLOOD);
    tex_door(&g_walls[WT_DOOR_GOLD], R_GOLD, R_GOLD);
    tex_jamb(&g_walls[WT_JAMB]);
    tex_exit(&g_walls[WT_EXIT]);

    for (int i = 0; i < FT_COUNT; i++) g_flats[i] = img_new(FS, FS);
    flat_stones(&g_flats[FT_FLAG], R_COOL, 9, 100, 5.5f, false);
    flat_marble(&g_flats[FT_MARBLE]);
    flat_woodfloor(&g_flats[FT_WOOD]);
    flat_dirt(&g_flats[FT_DIRT]);
    flat_carpet(&g_flats[FT_CARPET]);
    flat_stones(&g_flats[FT_COBBLE], R_COOL, 30, 200, 5.0f, true);
    flat_bloodstone(&g_flats[FT_BLOODSTONE]);
    flat_beams(&g_flats[FT_BEAMS]);
    flat_vault(&g_flats[FT_VAULT]);

    for (int i = 0; i < 2; i++) { g_sky[i] = img_new(SKY_W, SKY_H); gen_sky(&g_sky[i], i == 1); }

    spr_candle(&g_spr[SPR_CANDLE0], 0);
    spr_candle(&g_spr[SPR_CANDLE1], 1);
    for (int i = 0; i < 3; i++) spr_brazier(&g_spr[SPR_BRAZIER0 + i], i);
    spr_chandelier(&g_spr[SPR_CHANDELIER0], 0);
    spr_chandelier(&g_spr[SPR_CHANDELIER1], 1);
    spr_pillar(&g_spr[SPR_PILLAR]);
    spr_grave(&g_spr[SPR_GRAVE]);
    spr_cross(&g_spr[SPR_CROSS]);
    spr_tree(&g_spr[SPR_TREE]);
    spr_skulls(&g_spr[SPR_SKULLS]);
    spr_coffin(&g_spr[SPR_COFFIN]);
    spr_cage(&g_spr[SPR_CAGE]);
    spr_potion(&g_spr[SPR_POTION]);
    spr_chalice(&g_spr[SPR_CHALICE]);
    spr_bolts(&g_spr[SPR_BOLTS]);
    spr_shot(&g_spr[SPR_SHOT]);
    spr_blunder_pick(&g_spr[SPR_BLUNDER_PICK]);
    spr_key(&g_spr[SPR_KEY_RED], R_BLOOD, R_BLOOD);
    spr_key(&g_spr[SPR_KEY_GOLD], R_GOLD, R_BLUE);

    draw_ghoul(&g_spr[SPR_GHOUL_WALK0], (GPose){1, 0, 0.3f, 0, 0.3f});
    draw_ghoul(&g_spr[SPR_GHOUL_WALK1], (GPose){-1, 0, 0.3f, 0, 0.3f});
    draw_ghoul(&g_spr[SPR_GHOUL_ATTACK], (GPose){0, 1, 0.6f, 0, 1});
    draw_ghoul(&g_spr[SPR_GHOUL_PAIN], (GPose){0, 0.3f, -0.6f, 1, 1});
    draw_ghoul(&g_spr[SPR_GHOUL_DIE0], (GPose){0, 0.6f, -0.9f, 6, 1});
    draw_ghoul(&g_spr[SPR_GHOUL_DIE1], (GPose){0.5f, 0.2f, -1.2f, 15, 1});
    draw_ghoul_dead(&g_spr[SPR_GHOUL_DEAD]);

    draw_skeleton(&g_spr[SPR_SKEL_WALK0], (SPose){1, 0, 0, 0, 0, false});
    draw_skeleton(&g_spr[SPR_SKEL_WALK1], (SPose){-1, 0, 0, 0, 0, false});
    draw_skeleton(&g_spr[SPR_SKEL_ATTACK0], (SPose){0, 0, 0, -0.3f, 1, false});
    draw_skeleton(&g_spr[SPR_SKEL_ATTACK1], (SPose){0, 0, 0, 0.3f, 2, false});
    draw_skeleton(&g_spr[SPR_SKEL_PAIN], (SPose){0, 0, 1, -0.6f, 0, false});
    draw_skeleton(&g_spr[SPR_SKEL_DIE0], (SPose){0.6f, 0, 10, -0.8f, 0, true});
    draw_skeleton_dead(&g_spr[SPR_SKEL_DEAD]);

    draw_wraith(&g_spr[SPR_WRAITH0], (WPose){0, 0, 0, 0});
    draw_wraith(&g_spr[SPR_WRAITH1], (WPose){2.2f, 0, 0, 0});
    draw_wraith(&g_spr[SPR_WRAITH_ATTACK], (WPose){1, 1, 0, 0});
    draw_wraith(&g_spr[SPR_WRAITH_PAIN], (WPose){3, 0.3f, 0.15f, 0});
    draw_wraith(&g_spr[SPR_WRAITH_DIE0], (WPose){1.5f, 0.5f, 0.45f, 0.2f});
    draw_wraith(&g_spr[SPR_WRAITH_DIE1], (WPose){2.5f, 0.8f, 0.8f, 0.45f});

    draw_bat(&g_spr[SPR_BAT0], 0);
    draw_bat(&g_spr[SPR_BAT1], 1);
    draw_bat_dead(&g_spr[SPR_BAT_DEAD]);

    draw_demon(&g_spr[SPR_DEMON_WALK0], (DPose){1, 0, 0, false});
    draw_demon(&g_spr[SPR_DEMON_WALK1], (DPose){-1, 0, 0, false});
    draw_demon(&g_spr[SPR_DEMON_ATTACK], (DPose){0, 1, 0, true});
    draw_demon(&g_spr[SPR_DEMON_PAIN], (DPose){0, 0.3f, 2, true});
    draw_demon(&g_spr[SPR_DEMON_DIE0], (DPose){0, 0.2f, 12, true});
    draw_demon(&g_spr[SPR_DEMON_DIE1], (DPose){0.4f, 0, 30, false});
    draw_demon_dead(&g_spr[SPR_DEMON_DEAD]);

    orb(&g_spr[SPR_FIREBALL0], 16, R_FIRE, R_FIRE, R_GOLD, 0);
    orb(&g_spr[SPR_FIREBALL1], 16, R_FIRE, R_FIRE, R_GOLD, 1);
    orb(&g_spr[SPR_SPECTRAL0], 16, R_PURPLE, R_SPECTRE, R_SPECTRE, 0);
    orb(&g_spr[SPR_SPECTRAL1], 16, R_PURPLE, R_SPECTRE, R_SPECTRE, 1);
    orb(&g_spr[SPR_HELLBALL0], 24, R_BLOOD, R_FIRE, R_GOLD, 0);
    orb(&g_spr[SPR_HELLBALL1], 24, R_BLOOD, R_FIRE, R_GOLD, 1);
    for (int i = 0; i < 3; i++) explosion(&g_spr[SPR_EXPLODE0 + i], i);

    draw_dagger(&g_wpn[WPN_DAGGER_IDLE], 118, 104, -2.0f);
    draw_dagger(&g_wpn[WPN_DAGGER_SWING0], 96, 98, -2.6f);
    draw_dagger(&g_wpn[WPN_DAGGER_SWING1], 70, 110, -3.0f);
    draw_crossbow(&g_wpn[WPN_XBOW_IDLE], true);
    draw_crossbow(&g_wpn[WPN_XBOW_FIRED], false);
    draw_blunderbuss(&g_wpn[WPN_BLUNDER_IDLE], false);
    draw_blunderbuss(&g_wpn[WPN_BLUNDER_FIRE], true);
    draw_flash(&g_wpn[WPN_FLASH]);
}
