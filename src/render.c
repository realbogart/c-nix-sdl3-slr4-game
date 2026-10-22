/*
 * Software renderer: DDA raycast walls with sliding doors, perspective
 * floor/ceiling casting with sky for open-air cells, depth-sorted
 * billboards, particles, the first-person weapon and the HUD.
 * Everything renders into an 8-bit palettised framebuffer.
 */
#include "common.h"

u8 g_fb[SCR_W * SCR_H];

#define FOV_TAN 0.80f
#define WALL_H 1.5f
#define FOG_K 0.085f

static float s_zbuf[SCR_W];
static int s_top[SCR_W], s_bot[SCR_W];
static float s_focal, s_horizon;
static float s_dirx, s_diry, s_plx, s_ply;

static const float DITHER[4][4] = {
    {0.03f, 0.53f, 0.16f, 0.66f}, {0.78f, 0.28f, 0.91f, 0.41f},
    {0.22f, 0.72f, 0.09f, 0.59f}, {0.97f, 0.47f, 0.84f, 0.34f},
};

static inline float lantern(float d) { return d < 4.5f ? 0.22f * (1.0f - d / 4.5f) : 0.0f; }

static inline int light_level(float l, int x, int y)
{
    int v = (int)(l + DITHER[y & 3][x & 3]);
    return v < 0 ? 0 : v > NLIGHT - 1 ? NLIGHT - 1 : v;
}

void render_fill(u8 col) { memset(g_fb, col, sizeof g_fb); }

void render_box(int x, int y, int w, int h, u8 col)
{
    for (int yy = y; yy < y + h; yy++) {
        if ((unsigned)yy >= SCR_H) continue;
        for (int xx = x; xx < x + w; xx++)
            if ((unsigned)xx < SCR_W) g_fb[yy * SCR_W + xx] = col;
    }
}

void render_darken(int level)
{
    const u8 *cm = g_cmap[clampi(level, 0, NLIGHT - 1)];
    for (int i = 0; i < SCR_W * SCR_H; i++) g_fb[i] = cm[g_fb[i]];
}

void render_image(const Image *im, int x0, int y0, int lvl)
{
    const u8 *cm = g_cmap[clampi(lvl, 0, NLIGHT - 1)];
    for (int x = 0; x < im->w; x++) {
        int sx = x0 + x;
        if ((unsigned)sx >= SCR_W) continue;
        const u8 *col = im->px + x * im->h;
        for (int y = 0; y < im->h; y++) {
            int sy = y0 + y;
            if ((unsigned)sy >= SCR_H || col[y] == TRANSP) continue;
            g_fb[sy * SCR_W + sx] = cm[col[y]];
        }
    }
}

static void image_add(const Image *im, int x0, int y0)
{
    for (int x = 0; x < im->w; x++) {
        int sx = x0 + x;
        if ((unsigned)sx >= SCR_W) continue;
        const u8 *col = im->px + x * im->h;
        for (int y = 0; y < im->h; y++) {
            int sy = y0 + y;
            if ((unsigned)sy >= SCR_H || col[y] == TRANSP) continue;
            u8 *d = &g_fb[sy * SCR_W + sx];
            *d = g_addt[col[y] * 256 + *d];
        }
    }
}

/* ------------------------------------------------------------------ */
/* walls                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    bool hit;
    float perp, u, lx, ly;
    int tex, side;
} Hit;

static void cast(float px, float py, float rdx, float rdy, Hit *h)
{
    int mx = (int)floorf(px), my = (int)floorf(py);
    float ddx = rdx == 0 ? 1e30f : fabsf(1.0f / rdx);
    float ddy = rdy == 0 ? 1e30f : fabsf(1.0f / rdy);
    int sx, sy;
    float sdx, sdy;
    if (rdx < 0) { sx = -1; sdx = (px - mx) * ddx; } else { sx = 1; sdx = (mx + 1.0f - px) * ddx; }
    if (rdy < 0) { sy = -1; sdy = (py - my) * ddy; } else { sy = 1; sdy = (my + 1.0f - py) * ddy; }
    Cell *start = map_cell(mx, my);
    bool prev_door = start && start->door >= 0;
    h->hit = false;
    for (int i = 0; i < 160; i++) {
        int side;
        if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
        else { sdy += ddy; my += sy; side = 1; }
        Cell *c = map_cell(mx, my);
        if (!c) return;
        c->flags |= CF_SEEN;
        if (c->door >= 0) {
            const Door *d = &G.doors[c->door];
            float t, fr;
            if (d->axis == 0) {
                t = (mx + 0.5f - px) / rdx;
                float hy = py + t * rdy;
                fr = hy - my;
            } else {
                t = (my + 0.5f - py) / rdy;
                float hx = px + t * rdx;
                fr = hx - mx;
            }
            if (t > 0 && fr >= 0 && fr < 1 && fr >= d->open) {
                h->hit = true;
                h->perp = t;
                h->u = fr - d->open;
                h->tex = d->tex;
                h->side = d->axis;
                h->lx = px + rdx * (t - 0.02f);
                h->ly = py + rdy * (t - 0.02f);
                return;
            }
            prev_door = true;
            continue;
        }
        if (c->wall) {
            float perp = side == 0 ? sdx - ddx : sdy - ddy;
            float wx = side == 0 ? py + perp * rdy : px + perp * rdx;
            wx -= floorf(wx);
            if ((side == 0 && rdx > 0) || (side == 1 && rdy < 0)) wx = 1 - wx;
            h->hit = true;
            h->perp = perp;
            h->u = wx;
            h->tex = prev_door ? WT_JAMB : c->wall - 1;
            h->side = side;
            h->lx = px + rdx * (perp - 0.02f);
            h->ly = py + rdy * (perp - 0.02f);
            return;
        }
        prev_door = false;
    }
}

static void draw_walls(void)
{
    const Player *p = &G.pl;
    for (int x = 0; x < SCR_W; x++) {
        float camx = 2.0f * (x + 0.5f) / SCR_W - 1.0f;
        float rdx = s_dirx + s_plx * camx, rdy = s_diry + s_ply * camx;
        Hit h;
        cast(p->x, p->y, rdx, rdy, &h);
        if (!h.hit || h.perp < 0.02f) {
            s_zbuf[x] = 1e9f;
            s_top[x] = (int)s_horizon;
            s_bot[x] = (int)s_horizon - 1;
            continue;
        }
        s_zbuf[x] = h.perp;
        float scale = s_focal / h.perp;
        float ytop = s_horizon - (WALL_H - p->eye) * scale;
        float ybot = s_horizon + p->eye * scale;
        int y0 = (int)ceilf(ytop - 0.5f), y1 = (int)floorf(ybot - 0.5f);
        if (y0 < 0) y0 = 0;
        if (y1 > SCR_H - 1) y1 = SCR_H - 1;
        s_top[x] = y0;
        s_bot[x] = y1;
        const Image *tex = &g_walls[h.tex];
        int tu = clampi((int)(h.u * tex->w), 0, tex->w - 1);
        const u8 *col = tex->px + tu * tex->h;
        float vstep = tex->h / (ybot - ytop);
        float v = (y0 + 0.5f - ytop) * vstep;
        float lf = (light_at(h.lx, h.ly) * expf(-h.perp * FOG_K) + lantern(h.perp)) * LIGHT_ONE;
        if (h.side) lf *= 0.86f;
        for (int y = y0; y <= y1; y++) {
            int tv = (int)v;
            if (tv >= tex->h) tv = tex->h - 1;
            g_fb[y * SCR_W + x] = g_cmap[light_level(lf, x, y)][col[tv]];
            v += vstep;
        }
    }
}

/* ------------------------------------------------------------------ */
/* floors, ceilings, sky                                               */
/* ------------------------------------------------------------------ */
static void draw_flats(void)
{
    const Player *p = &G.pl;
    const Image *sky = &g_sky[g_levels[G.level].sky];
    static int skyu[SCR_W];
    for (int x = 0; x < SCR_W; x++) {
        float camx = 2.0f * (x + 0.5f) / SCR_W - 1.0f;
        float a = p->ang + atanf(camx * FOV_TAN);
        int u = (int)floorf(a / PI * SKY_W);
        skyu[x] = ((u % SKY_W) + SKY_W) % SKY_W;
    }
    int skylvl = clampi((int)(LIGHT_ONE + G.lightning * 10), 0, NLIGHT - 1);
    const u8 *skycm = g_cmap[skylvl];

    for (int y = 0; y < SCR_H; y++) {
        float fy = y + 0.5f;
        bool floor = fy > s_horizon;
        float dy = floor ? fy - s_horizon : s_horizon - fy;
        if (dy < 0.5f) dy = 0.5f;
        float rowdist = (floor ? p->eye : WALL_H - p->eye) * s_focal / dy;
        float fog = expf(-rowdist * FOG_K), lan = lantern(rowdist);
        float cam0 = -1.0f + 1.0f / SCR_W;
        float wx = p->x + rowdist * (s_dirx + s_plx * cam0);
        float wy = p->y + rowdist * (s_diry + s_ply * cam0);
        float stx = rowdist * 2.0f * s_plx / SCR_W, sty = rowdist * 2.0f * s_ply / SCR_W;
        int sv = clampi((int)(y - s_horizon + SKY_HORIZON), 0, SKY_H - 1);
        u8 *row = &g_fb[y * SCR_W];
        for (int x = 0; x < SCR_W; x++, wx += stx, wy += sty) {
            if (floor ? y <= s_bot[x] : y >= s_top[x]) continue;
            int cx = (int)floorf(wx), cy = (int)floorf(wy);
            Cell *c = map_cell(cx, cy);
            if (!c) {
                row[x] = floor ? 0 : skycm[sky->px[skyu[x] * SKY_H + sv]];
                continue;
            }
            if (!floor && (c->flags & CF_OUTDOOR)) {
                row[x] = skycm[sky->px[skyu[x] * SKY_H + sv]];
                continue;
            }
            const Image *t = &g_flats[floor ? c->floor : c->ceil];
            int tx = (int)(wx * 64) & 63, ty = (int)(wy * 64) & 63;
            u8 tc = t->px[tx * 64 + ty];
            float lf = (light_at(wx, wy) * fog + lan) * LIGHT_ONE;
            if (!floor) lf *= 0.8f;
            row[x] = g_cmap[light_level(lf, x, y)][tc];
        }
    }
}

/* ------------------------------------------------------------------ */
/* sprites                                                             */
/* ------------------------------------------------------------------ */
enum { SM_NORMAL, SM_TRANSLUCENT, SM_ADDITIVE };

typedef struct {
    float x, y, z, depth;
    const Image *img;
    int mode;
    bool ceiling;
    float bright;
} SprItem;

static SprItem s_items[MAX_THINGS + MAX_MOBS + MAX_PROJ + 64];
static int s_nitems;

static void push_sprite(float x, float y, float z, const Image *img, int mode, bool ceiling, float bright)
{
    float rx = x - G.pl.x, ry = y - G.pl.y;
    float depth = rx * s_dirx + ry * s_diry;
    if (depth < 0.12f) return;
    SprItem *s = &s_items[s_nitems++];
    s->x = x; s->y = y; s->z = z;
    s->depth = depth;
    s->img = img;
    s->mode = mode;
    s->ceiling = ceiling;
    s->bright = bright;
}

static int cmp_depth(const void *a, const void *b)
{
    float da = ((const SprItem *)a)->depth, db = ((const SprItem *)b)->depth;
    return da < db ? 1 : da > db ? -1 : 0;
}

static void draw_sprite(const SprItem *s)
{
    const Player *p = &G.pl;
    float rx = s->x - p->x, ry = s->y - p->y;
    float lat = rx * -s_diry + ry * s_dirx;
    float scale = s_focal / s->depth;
    float sx = SCR_W * 0.5f + lat * scale;
    float ww = s->img->w / 64.0f, wh = s->img->h / 64.0f;
    float pw = ww * scale, ph = wh * scale;
    float zb = s->ceiling ? WALL_H - wh : s->z;
    float ytop = s_horizon - (zb + wh - p->eye) * scale;
    float xl = sx - pw * 0.5f;
    int x0 = (int)ceilf(xl - 0.5f), x1 = (int)floorf(xl + pw - 0.5f);
    int y0 = (int)ceilf(ytop - 0.5f), y1 = (int)floorf(ytop + ph - 0.5f);
    if (x1 < 0 || x0 >= SCR_W || y1 < 0 || y0 >= SCR_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > SCR_W - 1) x1 = SCR_W - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H - 1) y1 = SCR_H - 1;
    float lf = (light_at(s->x, s->y) * expf(-s->depth * FOG_K) + lantern(s->depth)) * LIGHT_ONE;
    lf = maxf(lf, s->bright);
    float ustep = s->img->w / pw, vstep = s->img->h / ph;
    for (int x = x0; x <= x1; x++) {
        if (s->depth >= s_zbuf[x]) continue;
        int tu = clampi((int)((x + 0.5f - xl) * ustep), 0, s->img->w - 1);
        const u8 *col = s->img->px + tu * s->img->h;
        float v = (y0 + 0.5f - ytop) * vstep;
        for (int y = y0; y <= y1; y++, v += vstep) {
            int tv = (int)v;
            if (tv >= s->img->h) break;
            u8 c = col[tv];
            if (c == TRANSP) continue;
            u8 *d = &g_fb[y * SCR_W + x];
            switch (s->mode) {
            case SM_NORMAL: *d = g_cmap[light_level(lf, x, y)][c]; break;
            case SM_TRANSLUCENT: *d = g_blend[g_cmap[light_level(lf, x, y)][c] * 256 + *d]; break;
            default: *d = g_addt[c * 256 + *d]; break;
            }
        }
    }
}

static void draw_sprites(void)
{
    s_nitems = 0;
    for (int i = 0; i < G.nthings; i++) {
        const Thing *t = &G.things[i];
        if (!t->alive) continue;
        const ThingInfo *ti = &g_thing_info[t->kind];
        float z = ti->pickup ? 0.04f + 0.035f * sinf(t->phase * 3.0f) : 0;
        push_sprite(t->x, t->y, z, &g_spr[thing_sprite(t)], SM_NORMAL, ti->ceiling, 0);
    }
    for (int i = 0; i < G.nmobs; i++) {
        const Mob *m = &G.mobs[i];
        int spr = mob_sprite(m);
        if (spr < 0) continue;
        const MobInfo *mi = &g_mob_info[m->type];
        push_sprite(m->x, m->y, m->z, &g_spr[spr], mi->translucent ? SM_TRANSLUCENT : SM_NORMAL, false,
                    m->flash > 0 ? 30.0f : 0);
    }
    for (int i = 0; i < MAX_PROJ; i++) {
        const Proj *pr = &G.proj[i];
        if (!pr->alive) continue;
        int f = ((int)(pr->life * 12)) & 1;
        int spr = pr->type == PJ_FIREBALL ? SPR_FIREBALL0 : pr->type == PJ_SPECTRAL ? SPR_SPECTRAL0 : SPR_HELLBALL0;
        const Image *im = &g_spr[spr + f];
        push_sprite(pr->x, pr->y, pr->z - im->h / 128.0f, im, SM_ADDITIVE, false, 0);
    }
    for (int i = 0; i < 64; i++) {
        const Blast *b = &G.blasts[i];
        if (b->life <= 0) continue;
        int f = clampi((int)(b->t / 0.1f), 0, 2);
        const Image *im = &g_spr[SPR_EXPLODE0 + f];
        push_sprite(b->x, b->y, b->z - im->h / 128.0f, im, f == 2 ? SM_TRANSLUCENT : SM_ADDITIVE, false, f == 2 ? 0 : 31);
    }
    qsort(s_items, (size_t)s_nitems, sizeof s_items[0], cmp_depth);
    for (int i = 0; i < s_nitems; i++) draw_sprite(&s_items[i]);
}

static void draw_particles(void)
{
    const Player *p = &G.pl;
    for (int i = 0; i < G.npart; i++) {
        const Particle *q = &G.parts[i];
        float rx = q->x - p->x, ry = q->y - p->y;
        float depth = rx * s_dirx + ry * s_diry;
        if (depth < 0.15f) continue;
        float lat = rx * -s_diry + ry * s_dirx;
        float scale = s_focal / depth;
        int sx = (int)(SCR_W * 0.5f + lat * scale);
        int sy = (int)(s_horizon - (q->z - p->eye) * scale);
        if ((unsigned)sx >= SCR_W || depth >= s_zbuf[sx]) continue;
        int sz = (int)(q->size / 32.0f * scale);
        if (sz < 1) sz = 1;
        if (sz > 6) sz = 6;
        u8 c = q->col;
        if (!q->glow) {
            float lf = (light_at(q->x, q->y) * expf(-depth * FOG_K) + lantern(depth)) * LIGHT_ONE;
            c = g_cmap[light_level(lf, sx, sy)][c];
        }
        for (int yy = sy; yy < sy + sz; yy++)
            for (int xx = sx; xx < sx + sz; xx++)
                if ((unsigned)xx < SCR_W && (unsigned)yy < SCR_H && depth < s_zbuf[xx]) g_fb[yy * SCR_W + xx] = c;
    }
}

static u32 hash_u(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void draw_rain(void)
{
    Cell *c = map_cell((int)G.pl.x, (int)G.pl.y);
    if (!g_levels[G.level].rain || !c || !(c->flags & CF_OUTDOOR)) return;
    u8 col = pal_index(R_NIGHT, 9);
    float t = G.time;
    float shift = G.pl.ang * 120.0f;
    for (int i = 0; i < 160; i++) {
        u32 h = hash_u((u32)i * 2654435761u + 7);
        float spd = 380 + (h & 255);
        int x = (int)((h >> 8) % (SCR_W + 40)) - 20 + (int)shift;
        x = ((x % SCR_W) + SCR_W) % SCR_W;
        int y = (int)fmodf((h >> 3) % SCR_H + t * spd, SCR_H + 20) - 10;
        for (int k = 0; k < 7; k++) {
            int px = x - k / 3, py = y + k;
            if ((unsigned)px < SCR_W && (unsigned)py < SCR_H) {
                u8 *d = &g_fb[py * SCR_W + px];
                *d = g_blend[col * 256 + *d];
            }
        }
    }
}

void render_world(void)
{
    const Player *p = &G.pl;
    s_dirx = cosf(p->ang);
    s_diry = sinf(p->ang);
    s_plx = -s_diry * FOV_TAN;
    s_ply = s_dirx * FOV_TAN;
    s_focal = (SCR_W * 0.5f) / FOV_TAN;
    s_horizon = SCR_H * 0.5f + p->pitch * SCR_H * 0.38f;
    draw_walls();
    draw_flats();
    draw_sprites();
    draw_particles();
    draw_rain();
}

/* ------------------------------------------------------------------ */
/* weapon                                                              */
/* ------------------------------------------------------------------ */
void render_weapon(void)
{
    const Player *p = &G.pl;
    if (p->dead) return;
    const Image *im;
    float ox = 0, oy = 0;
    bool flash = false;
    switch (p->weapon) {
    case W_DAGGER: {
        float a = p->wtimer > 0 ? 0.42f - p->wtimer : 1;
        im = &g_wpn[a < 0.07f ? WPN_DAGGER_SWING0 : a < 0.18f ? WPN_DAGGER_SWING1 : a < 0.28f ? WPN_DAGGER_SWING0 : WPN_DAGGER_IDLE];
        ox = 50;
        oy = -8;
        break;
    }
    case W_CROSSBOW:
        if (p->wtimer > 0.5f) { im = &g_wpn[WPN_XBOW_FIRED]; oy = (p->wtimer - 0.5f) * 30; }
        else if (p->wtimer > 0) {
            float t = (0.5f - p->wtimer) / 0.5f;
            im = &g_wpn[t < 0.5f ? WPN_XBOW_FIRED : WPN_XBOW_IDLE];
            oy = sinf(t * PI) * 40;
        } else im = &g_wpn[WPN_XBOW_IDLE];
        break;
    default:
        if (p->wtimer > 0.75f) {
            float t = 1.05f - p->wtimer;
            im = &g_wpn[t < 0.1f ? WPN_BLUNDER_FIRE : WPN_BLUNDER_IDLE];
            flash = t < 0.07f;
            oy = maxf(0, 1 - t / 0.3f) * 22;
        } else if (p->wtimer > 0) {
            float t = (0.75f - p->wtimer) / 0.75f;
            im = &g_wpn[WPN_BLUNDER_IDLE];
            oy = sinf(t * PI) * 34;
        } else im = &g_wpn[WPN_BLUNDER_IDLE];
        break;
    }
    float bx = cosf(p->bob) * 7 * p->bob_amt, by = fabsf(sinf(p->bob)) * 6 * p->bob_amt;
    int x = (int)(SCR_W / 2 - im->w / 2 + bx + ox);
    int y = (int)(SCR_H - im->h + 16 + by + oy + p->wswitch * 110);
    float lf = (light_at(p->x, p->y) + 0.25f) * LIGHT_ONE + (flash ? 8 : 0);
    render_image(im, x, y, clampi((int)lf, 6, NLIGHT - 1));
    if (flash) image_add(&g_wpn[WPN_FLASH], x + 110 - 60, y + 28 - 52);
}

/* ------------------------------------------------------------------ */
/* HUD                                                                 */
/* ------------------------------------------------------------------ */
static void hud_text_center(const char *s, int y, int scale, u8 col)
{
    font_draw_shadow(g_fb, (SCR_W - font_width(s, scale)) / 2, y, s, scale, col);
}

void render_hud(void)
{
    const Player *p = &G.pl;
    char buf[64];

    if (!p->dead) {
        u8 cc = pal_index(R_BONE, 13);
        g_fb[(SCR_H / 2) * SCR_W + SCR_W / 2] = cc;
        g_fb[(SCR_H / 2) * SCR_W + SCR_W / 2 + 1] = cc;
        g_fb[(SCR_H / 2 + 1) * SCR_W + SCR_W / 2] = cc;
        g_fb[(SCR_H / 2 + 1) * SCR_W + SCR_W / 2 + 1] = 0;
    }

    /* bottom vignette strip */
    for (int y = SCR_H - 30; y < SCR_H; y++) {
        int lvl = 24 - (y - (SCR_H - 30)) / 2;
        for (int x = 0; x < 120; x++) g_fb[y * SCR_W + x] = g_cmap[clampi(lvl + x / 10, 0, 31)][g_fb[y * SCR_W + x]];
        for (int x = SCR_W - 120; x < SCR_W; x++)
            g_fb[y * SCR_W + x] = g_cmap[clampi(lvl + (SCR_W - 1 - x) / 10, 0, 31)][g_fb[y * SCR_W + x]];
    }

    render_image(&g_spr[SPR_POTION], 8, SCR_H - 25, 24);
    snprintf(buf, sizeof buf, "%d", (int)ceilf(p->hp));
    u8 hc = p->hp > 50 ? pal_index(R_BONE, 13) : p->hp > 25 ? pal_index(R_GOLD, 13) : pal_index(R_BLOOD, 14);
    font_draw_shadow(g_fb, 26, SCR_H - 21, buf, 2, hc);
    int kx = 26 + font_width(buf, 2) + 8;
    if (p->key_red) { render_image(&g_spr[SPR_KEY_RED], kx, SCR_H - 22, 26); kx += 24; }
    if (p->key_gold) render_image(&g_spr[SPR_KEY_GOLD], kx, SCR_H - 22, 26);

    const char *wname = p->weapon == W_DAGGER ? "SILVER DAGGER" : p->weapon == W_CROSSBOW ? "CROSSBOW" : "BLUNDERBUSS";
    font_draw_shadow(g_fb, SCR_W - 8 - font_width(wname, 1), SCR_H - 34, wname, 1, pal_index(R_BONE, 9));
    if (p->weapon != W_DAGGER) {
        int ammo = p->weapon == W_CROSSBOW ? p->bolts : p->shot;
        snprintf(buf, sizeof buf, "%d", ammo);
        int w = font_width(buf, 2);
        u8 ac = ammo > 5 ? pal_index(R_BONE, 13) : pal_index(R_BLOOD, 14);
        font_draw_shadow(g_fb, SCR_W - 8 - w, SCR_H - 21, buf, 2, ac);
        const Image *icon = &g_spr[p->weapon == W_CROSSBOW ? SPR_BOLTS : SPR_SHOT];
        render_image(icon, SCR_W - 14 - w - icon->w, SCR_H - 20, 24);
    }

    if (G.msg_t > 0 && ((int)(G.msg_t * 8) & 1 || G.msg_t < 2.6f))
        font_draw_shadow(g_fb, 6, 6, G.msg, 1, pal_index(R_BONE, 13));

    if (G.title_t > 0) {
        const LevelDef *L = &g_levels[G.level];
        hud_text_center(L->name, 64, 3, pal_index(R_GOLD, 14));
        hud_text_center(L->subtitle, 92, 1, pal_index(R_BONE, 12));
    }

    if (G.boss_alive && G.boss_idx >= 0) {
        const Mob *b = &G.mobs[G.boss_idx];
        if (b->state != MS_IDLE) {
            int w = 200, x = (SCR_W - w) / 2, y = 18;
            hud_text_center("ABADDON", 8, 1, pal_index(R_BLOOD, 14));
            render_box(x - 1, y - 1, w + 2, 7, pal_index(R_GOLD, 6));
            render_box(x, y, w, 5, pal_index(R_GRAY, 1));
            render_box(x, y, (int)(w * clampf(b->hp / g_mob_info[MOB_DEMON].hp, 0, 1)), 5, pal_index(R_BLOOD, 13));
        }
    }
}

void render_automap(void)
{
    render_darken(5);
    const Player *p = &G.pl;
    const int S = 6;
    int ox = SCR_W / 2 - (int)(p->x * S), oy = SCR_H / 2 - (int)(p->y * S);
    for (int y = 0; y < G.h; y++)
        for (int x = 0; x < G.w; x++) {
            Cell *c = map_cell(x, y);
            bool near_seen = false;
            for (int k = 0; k < 9 && !near_seen; k++) {
                Cell *n = map_cell(x - 1 + k % 3, y - 1 + k / 3);
                if (n && (n->flags & CF_SEEN)) near_seen = true;
            }
            if (!near_seen) continue;
            u8 col;
            if (c->door >= 0) {
                int lk = G.doors[c->door].lock;
                col = lk == LOCK_RED ? pal_index(R_BLOOD, 13) : lk == LOCK_GOLD ? pal_index(R_GOLD, 13) : pal_index(R_WOOD, 10);
            } else if (c->wall == WT_EXIT + 1) col = pal_index(R_SPECTRE, 14);
            else if (c->wall) col = pal_index(R_COOL, 10);
            else if (c->flags & CF_SEEN) col = pal_index(R_NIGHT, 5);
            else continue;
            render_box(ox + x * S, oy + y * S, S - (c->wall ? 0 : 1), S - (c->wall ? 0 : 1), col);
        }
    float dx = cosf(p->ang), dy = sinf(p->ang);
    for (int i = -2; i <= 8; i++) {
        float w = i < 0 ? 0 : (8 - i) * 0.4f;
        for (float s = -w; s <= w; s += 0.5f) {
            int px = SCR_W / 2 + (int)(dx * (i - 3) - dy * s), py = SCR_H / 2 + (int)(dy * (i - 3) + dx * s);
            if ((unsigned)px < SCR_W && (unsigned)py < SCR_H) g_fb[py * SCR_W + px] = pal_index(R_GOLD, 15);
        }
    }
    hud_text_center("MAP", 8, 1, pal_index(R_GOLD, 13));
}

void render_to_rgb(u32 *out, float red, float gold, float white)
{
    u32 pal[256];
    for (int i = 0; i < 256; i++) {
        float r = (g_pal[i] >> 16) & 255, g = (g_pal[i] >> 8) & 255, b = g_pal[i] & 255;
        r += (255 - r) * red * 0.55f;
        g -= g * red * 0.45f;
        b -= b * red * 0.45f;
        r += (255 - r) * gold * 0.25f;
        g += (220 - g) * gold * 0.22f;
        b -= b * gold * 0.1f;
        r += (255 - r) * white;
        g += (255 - g) * white;
        b += (255 - b) * white;
        pal[i] = (u32)clampi((int)r, 0, 255) << 16 | (u32)clampi((int)g, 0, 255) << 8 | (u32)clampi((int)b, 0, 255);
    }
    for (int i = 0; i < SCR_W * SCR_H; i++) out[i] = pal[g_fb[i]];
}
