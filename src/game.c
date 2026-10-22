/*
 * Game simulation: player, weapons, doors, monster AI, projectiles,
 * particles and the dynamic light map.
 */
#include "common.h"
#include <stdarg.h>

Game G;
float g_sens = 1.0f;

#define MAX_LW 60000
static LightW s_lw[MAX_LW];
static int s_nlw;
static float s_muzzle;
static float s_hurt_snd_cd;

/* ------------------------------------------------------------------ */
/* random                                                              */
/* ------------------------------------------------------------------ */
static u32 s_rng = 0x9e3779b9u;

u32 rng_next(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

float frand(void) { return (rng_next() & 0xffffff) / 16777216.0f; }
float frand2(float lo, float hi) { return lo + (hi - lo) * frand(); }
int irand(int lo, int hi) { return lo + (int)(rng_next() % (u32)(hi - lo + 1)); }

float wrap_angle(float a)
{
    while (a > PI) a -= TAU;
    while (a <= -PI) a += TAU;
    return a;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */
const ThingInfo g_thing_info[TH_COUNT] = {
    [TH_CANDLE] = {SPR_CANDLE0, 2, 6, true, false, false, 0.55f, 4.5f, 0.12f},
    [TH_BRAZIER] = {SPR_BRAZIER0, 3, 9, true, false, false, 1.0f, 7.0f, 0.22f},
    [TH_CHANDELIER] = {SPR_CHANDELIER0, 2, 5, false, true, false, 0.8f, 6.5f, 0.08f},
    [TH_PILLAR] = {SPR_PILLAR, 1, 0, true, false, false, 0, 0, 0},
    [TH_GRAVE] = {SPR_GRAVE, 1, 0, true, false, false, 0, 0, 0},
    [TH_CROSS] = {SPR_CROSS, 1, 0, true, false, false, 0, 0, 0},
    [TH_TREE] = {SPR_TREE, 1, 0, true, false, false, 0, 0, 0},
    [TH_SKULLS] = {SPR_SKULLS, 1, 0, false, false, false, 0, 0, 0},
    [TH_COFFIN] = {SPR_COFFIN, 1, 0, true, false, false, 0, 0, 0},
    [TH_CAGE] = {SPR_CAGE, 1, 0, false, true, false, 0, 0, 0},
    [TH_POTION] = {SPR_POTION, 1, 0, false, false, true, 0, 0, 0},
    [TH_CHALICE] = {SPR_CHALICE, 1, 0, false, false, true, 0.25f, 2.0f, 0},
    [TH_BOLTS] = {SPR_BOLTS, 1, 0, false, false, true, 0, 0, 0},
    [TH_SHOT] = {SPR_SHOT, 1, 0, false, false, true, 0, 0, 0},
    [TH_BLUNDER] = {SPR_BLUNDER_PICK, 1, 0, false, false, true, 0, 0, 0},
    [TH_KEY_RED] = {SPR_KEY_RED, 1, 0, false, false, true, 0.3f, 2.5f, 0},
    [TH_KEY_GOLD] = {SPR_KEY_GOLD, 1, 0, false, false, true, 0.3f, 2.5f, 0},
};

const MobInfo g_mob_info[MOB_COUNT] = {
    [MOB_GHOUL] = {"GHOUL", 60, 1.9f, 0.32f, 1.0f, 0.0f, 8, 14, 0.9f, -1, 0, 0.9f, 0.6f, false, false, false,
                   SND_GHOUL_SIGHT, SND_GHOUL_PAIN, SND_GHOUL_DIE,
                   SPR_GHOUL_WALK0, SPR_GHOUL_WALK1, SPR_GHOUL_ATTACK, SPR_GHOUL_ATTACK, SPR_GHOUL_ATTACK,
                   SPR_GHOUL_PAIN, SPR_GHOUL_DIE0, SPR_GHOUL_DIE1, SPR_GHOUL_DEAD},
    [MOB_SKELETON] = {"SKELETON", 45, 1.6f, 0.3f, 1.0f, 0.0f, 6, 10, 0.9f, PJ_FIREBALL, 0.8f, 1.6f, 0.5f, false, false, false,
                      SND_SKEL_SIGHT, SND_SKEL_SIGHT, SND_SKEL_DIE,
                      SPR_SKEL_WALK0, SPR_SKEL_WALK1, SPR_SKEL_ATTACK1, SPR_SKEL_ATTACK0, SPR_SKEL_ATTACK1,
                      SPR_SKEL_PAIN, SPR_SKEL_DIE0, SPR_SKEL_DIE0, SPR_SKEL_DEAD},
    [MOB_WRAITH] = {"WRAITH", 80, 2.2f, 0.32f, 1.0f, 0.12f, 10, 16, 1.0f, PJ_SPECTRAL, 0.7f, 1.8f, 0.3f, true, true, false,
                    SND_WRAITH_SIGHT, SND_WRAITH_SIGHT, SND_WRAITH_DIE,
                    SPR_WRAITH0, SPR_WRAITH1, SPR_WRAITH_ATTACK, SPR_WRAITH_ATTACK, SPR_WRAITH_ATTACK,
                    SPR_WRAITH_PAIN, SPR_WRAITH_DIE0, SPR_WRAITH_DIE1, -1},
    [MOB_BAT] = {"BAT", 16, 4.0f, 0.22f, 1.0f, 0.62f, 3, 7, 0.8f, -1, 0, 0.7f, 0.9f, true, false, false,
                 SND_BAT, SND_BAT, SND_BAT_DIE,
                 SPR_BAT0, SPR_BAT1, SPR_BAT1, SPR_BAT1, SPR_BAT1, SPR_BAT0, SPR_BAT_DEAD, SPR_BAT_DEAD, SPR_BAT_DEAD},
    [MOB_DEMON] = {"ABADDON", 1100, 1.7f, 0.6f, 1.0f, 0.0f, 25, 40, 1.5f, PJ_HELLBALL, 1.1f, 1.3f, 0.08f, false, false, true,
                   SND_DEMON_SIGHT, SND_DEMON_PAIN, SND_DEMON_DIE,
                   SPR_DEMON_WALK0, SPR_DEMON_WALK1, SPR_DEMON_ATTACK, SPR_DEMON_ATTACK, SPR_DEMON_ATTACK,
                   SPR_DEMON_PAIN, SPR_DEMON_DIE0, SPR_DEMON_DIE1, SPR_DEMON_DEAD},
};

#define DIE_TIME 0.7f

int mob_sprite(const Mob *m)
{
    const MobInfo *mi = &g_mob_info[m->type];
    float fps = mi->flying ? 9.0f : 4.0f;
    switch (m->state) {
    case MS_IDLE: return mi->flying ? (((int)(m->anim * fps)) & 1 ? mi->spr_walk1 : mi->spr_walk0) : mi->spr_walk0;
    case MS_CHASE: return ((int)(m->anim * fps * (mi->speed / 1.8f))) & 1 ? mi->spr_walk1 : mi->spr_walk0;
    case MS_MELEE: return m->t < 0.3f ? mi->spr_melee : mi->spr_ranged1;
    case MS_RANGED: return m->t < 0.45f ? mi->spr_ranged0 : mi->spr_ranged1;
    case MS_PAIN: return mi->spr_pain;
    case MS_DYING: return m->t < DIE_TIME * 0.5f ? mi->spr_die0 : mi->spr_die1;
    default: return mi->spr_dead;
    }
}

int thing_sprite(const Thing *t)
{
    const ThingInfo *ti = &g_thing_info[t->kind];
    if (ti->nframes <= 1) return ti->spr;
    return ti->spr + ((int)(t->phase * ti->fps)) % ti->nframes;
}

/* ------------------------------------------------------------------ */
/* map helpers                                                         */
/* ------------------------------------------------------------------ */
Cell *map_cell(int x, int y)
{
    if (x < 0 || y < 0 || x >= G.w || y >= G.h) return NULL;
    return &G.cells[y * MAP_MAX + x];
}

bool map_solid(int x, int y)
{
    Cell *c = map_cell(x, y);
    if (!c || c->wall) return true;
    if (c->door >= 0 && G.doors[c->door].open < 0.9f) return true;
    return false;
}

bool map_blocks_move(int x, int y)
{
    Cell *c = map_cell(x, y);
    return !c || map_solid(x, y) || (c->flags & CF_BLOCK);
}

static bool los(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(hypotf(dx, dy) / 0.12f) + 1;
    for (int i = 1; i < n; i++) {
        float t = (float)i / n;
        if (map_solid((int)floorf(x0 + dx * t), (int)floorf(y0 + dy * t))) return false;
    }
    return true;
}

/* light passes through doors */
static bool los_light(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(hypotf(dx, dy) / 0.1f) + 1;
    int sx = (int)floorf(x0), sy = (int)floorf(y0), ex = (int)floorf(x1), ey = (int)floorf(y1);
    for (int i = 1; i < n; i++) {
        float t = (float)i / n;
        int cx = (int)floorf(x0 + dx * t), cy = (int)floorf(y0 + dy * t);
        if ((cx == sx && cy == sy) || (cx == ex && cy == ey)) continue;
        Cell *c = map_cell(cx, cy);
        if (!c || c->wall) return false;
    }
    return true;
}

void game_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(G.msg, sizeof G.msg, fmt, ap);
    va_end(ap);
    G.msg_t = 3.0f;
}

/* ------------------------------------------------------------------ */
/* spawning                                                            */
/* ------------------------------------------------------------------ */
void spawn_thing(int kind, float x, float y)
{
    if (G.nthings >= MAX_THINGS) return;
    Thing *t = &G.things[G.nthings++];
    memset(t, 0, sizeof *t);
    t->kind = kind;
    t->x = x;
    t->y = y;
    t->alive = true;
    t->phase = frand() * 10;
    if (g_thing_info[kind].block) {
        Cell *c = map_cell((int)x, (int)y);
        if (c) c->flags |= CF_BLOCK;
    }
}

void spawn_mob(int type, float x, float y)
{
    if (G.nmobs >= MAX_MOBS) return;
    Mob *m = &G.mobs[G.nmobs++];
    memset(m, 0, sizeof *m);
    m->type = type;
    m->x = x;
    m->y = y;
    m->z = g_mob_info[type].z;
    m->hp = g_mob_info[type].hp;
    m->state = MS_IDLE;
    m->anim = frand() * 5;
    m->sight_cd = frand() * 0.3f;
    m->strafe = frand() < 0.5f ? -1 : 1;
}

static void spawn_proj(int type, float x, float y, float z, float ang, float speed, float dmg)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &G.proj[i];
        if (p->alive) continue;
        p->type = type;
        p->x = x; p->y = y; p->z = z;
        p->vx = cosf(ang) * speed;
        p->vy = sinf(ang) * speed;
        p->dmg = dmg;
        p->life = 6;
        p->alive = true;
        return;
    }
}

static void spawn_particle(float x, float y, float z, float vx, float vy, float vz, float life,
                           u8 col, int size, bool grav, bool glow)
{
    if (G.npart >= MAX_PARTICLES) return;
    Particle *p = &G.parts[G.npart++];
    p->x = x; p->y = y; p->z = z;
    p->vx = vx; p->vy = vy; p->vz = vz;
    p->life = life;
    p->col = col;
    p->size = (u8)size;
    p->grav = grav;
    p->glow = glow;
}

static void spawn_blast(float x, float y, float z)
{
    for (int i = 0; i < 64; i++) {
        Blast *b = &G.blasts[i];
        if (b->life > 0) continue;
        b->x = x; b->y = y; b->z = z;
        b->life = 0.3f;
        b->t = 0;
        return;
    }
}

static void blood_burst(float x, float y, float z, int n, float ang)
{
    for (int i = 0; i < n; i++) {
        float a = ang + frand2(-0.9f, 0.9f), sp = frand2(0.4f, 2.4f);
        spawn_particle(x, y, z, cosf(a) * sp, sinf(a) * sp, frand2(0.3f, 2.8f), frand2(0.6f, 1.4f),
                       pal_index(R_BLOOD, irand(3, 8)), irand(1, 2), true, false);
    }
}

/* ------------------------------------------------------------------ */
/* lighting                                                            */
/* ------------------------------------------------------------------ */
static void add_light(float x, float y, float power, float radius, float flicker)
{
    if (G.nlights >= MAX_LIGHTS) return;
    Light *l = &G.lights[G.nlights++];
    l->x = x; l->y = y;
    l->power = power; l->radius = radius; l->flicker = flicker;
    l->first = s_nlw;
    l->ncells = 0;
    int r = (int)ceilf(radius);
    for (int cy = (int)y - r; cy <= (int)y + r; cy++)
        for (int cx = (int)x - r; cx <= (int)x + r; cx++) {
            Cell *c = map_cell(cx, cy);
            if (!c || c->wall) continue;
            float d = hypotf(cx + 0.5f - x, cy + 0.5f - y);
            if (d >= radius) continue;
            if (!los_light(x, y, cx + 0.5f, cy + 0.5f)) continue;
            float w = powf(1.0f - d / radius, 1.4f);
            if (w < 0.01f || s_nlw >= MAX_LW) continue;
            s_lw[s_nlw].cell = (u16)(cy * MAP_MAX + cx);
            s_lw[s_nlw].w = w;
            s_nlw++;
            l->ncells++;
        }
}

void lights_rebuild(void)
{
    G.nlights = 0;
    s_nlw = 0;
    for (int i = 0; i < G.nthings; i++) {
        const Thing *t = &G.things[i];
        const ThingInfo *ti = &g_thing_info[t->kind];
        if (ti->power > 0 && t->alive) add_light(t->x, t->y, ti->power, ti->radius, ti->flicker);
    }
    static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int y = 0; y < G.h; y++)
        for (int x = 0; x < G.w; x++) {
            Cell *c = map_cell(x, y);
            if (c->wall != WT_EXIT + 1) continue;
            for (int k = 0; k < 4; k++) {
                Cell *n = map_cell(x + nb[k][0], y + nb[k][1]);
                if (n && !n->wall)
                    add_light(x + 0.5f + nb[k][0] * 0.6f, y + 0.5f + nb[k][1] * 0.6f, 0.75f, 4.0f, 0.05f);
            }
        }
}

static void dyn_light(float x, float y, float power, float radius)
{
    int r = (int)ceilf(radius);
    for (int cy = (int)y - r; cy <= (int)y + r; cy++)
        for (int cx = (int)x - r; cx <= (int)x + r; cx++) {
            Cell *c = map_cell(cx, cy);
            if (!c || c->wall) continue;
            float d = hypotf(cx + 0.5f - x, cy + 0.5f - y);
            if (d >= radius) continue;
            if (!los_light(x, y, cx + 0.5f, cy + 0.5f)) continue;
            G.cell_light[cy * MAP_MAX + cx] += power * powf(1.0f - d / radius, 1.4f);
        }
}

static void lights_update(void)
{
    const LevelDef *L = &g_levels[G.level];
    float amb = L->ambient;
    float out = (L->sky == 1 ? 0.34f : 0.30f) + G.lightning * 1.1f;
    for (int y = 0; y < G.h; y++)
        for (int x = 0; x < G.w; x++) {
            Cell *c = &G.cells[y * MAP_MAX + x];
            G.cell_light[y * MAP_MAX + x] = c->wall ? 0 : amb + ((c->flags & CF_OUTDOOR) ? out : 0);
        }
    float t = G.time;
    for (int i = 0; i < G.nlights; i++) {
        Light *l = &G.lights[i];
        float n = 0.5f + 0.25f * sinf(t * 13.1f + i * 7.3f) + 0.25f * sinf(t * 7.7f + i * 2.1f);
        l->cur = l->power * (1.0f - l->flicker * n);
        for (int k = 0; k < l->ncells; k++) {
            const LightW *w = &s_lw[l->first + k];
            G.cell_light[w->cell] += l->cur * w->w;
        }
    }
    if (s_muzzle > 0) dyn_light(G.pl.x, G.pl.y, s_muzzle, 7.0f);
    for (int i = 0; i < MAX_PROJ; i++)
        if (G.proj[i].alive) dyn_light(G.proj[i].x, G.proj[i].y, G.proj[i].type == PJ_HELLBALL ? 0.7f : 0.5f, 3.5f);
    for (int i = 0; i < 64; i++)
        if (G.blasts[i].life > 0) dyn_light(G.blasts[i].x, G.blasts[i].y, G.blasts[i].life * 3.0f, 4.5f);

    const int VW = MAP_MAX + 1;
    for (int y = 0; y <= G.h; y++)
        for (int x = 0; x <= G.w; x++) {
            float s = 0;
            int n = 0;
            for (int k = 0; k < 4; k++) {
                int cx = x - 1 + (k & 1), cy = y - 1 + (k >> 1);
                Cell *c = map_cell(cx, cy);
                if (!c || c->wall) continue;
                s += G.cell_light[cy * MAP_MAX + cx];
                n++;
            }
            G.vert_light[y * VW + x] = n ? s / n : 0;
        }
}

float light_at(float x, float y)
{
    const int VW = MAP_MAX + 1;
    x = clampf(x, 0, (float)G.w - 0.001f);
    y = clampf(y, 0, (float)G.h - 0.001f);
    int i = (int)x, j = (int)y;
    float fx = x - i, fy = y - j;
    const float *v = &G.vert_light[j * VW + i];
    return lerpf(lerpf(v[0], v[1], fx), lerpf(v[VW], v[VW + 1], fx), fy);
}

/* ------------------------------------------------------------------ */
/* movement & collision                                                */
/* ------------------------------------------------------------------ */
static bool blocked(float x, float y, float r)
{
    int x0 = (int)floorf(x - r), x1 = (int)floorf(x + r);
    int y0 = (int)floorf(y - r), y1 = (int)floorf(y + r);
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++)
            if (map_solid(cx, cy)) return true;
    for (int cy = (int)y - 1; cy <= (int)y + 1; cy++)
        for (int cx = (int)x - 1; cx <= (int)x + 1; cx++) {
            Cell *c = map_cell(cx, cy);
            if (!c || !(c->flags & CF_BLOCK)) continue;
            float dx = x - (cx + 0.5f), dy = y - (cy + 0.5f);
            if (dx * dx + dy * dy < sqr(r + 0.26f)) return true;
        }
    return false;
}

static bool bodies_block(float nx, float ny, float ox, float oy, float r, int self)
{
    if (self >= 0 && !G.pl.dead) {
        float d1 = sqr(nx - G.pl.x) + sqr(ny - G.pl.y), d0 = sqr(ox - G.pl.x) + sqr(oy - G.pl.y);
        if (d1 < sqr(r + 0.25f) && d1 < d0) return true;
    }
    for (int i = 0; i < G.nmobs; i++) {
        const Mob *m = &G.mobs[i];
        if (i == self || m->state >= MS_DYING) continue;
        float rr = sqr((r + g_mob_info[m->type].radius) * 0.85f);
        float d1 = sqr(nx - m->x) + sqr(ny - m->y), d0 = sqr(ox - m->x) + sqr(oy - m->y);
        if (d1 < rr && d1 < d0) return true;
    }
    return false;
}

/* returns true if movement was obstructed */
static bool move_body(float *x, float *y, float dx, float dy, float r, int self)
{
    float len = hypotf(dx, dy);
    int n = (int)(len / 0.08f) + 1;
    float sx = dx / n, sy = dy / n;
    bool hit = false;
    for (int i = 0; i < n; i++) {
        if (!blocked(*x + sx, *y, r) && !bodies_block(*x + sx, *y, *x, *y, r, self)) *x += sx;
        else hit = true;
        if (!blocked(*x, *y + sy, r) && !bodies_block(*x, *y + sy, *x, *y, r, self)) *y += sy;
        else hit = true;
    }
    return hit;
}

/* ------------------------------------------------------------------ */
/* doors                                                               */
/* ------------------------------------------------------------------ */
static bool door_occupied(const Door *d)
{
    float cx = d->x + 0.5f, cy = d->y + 0.5f;
    if (fabsf(G.pl.x - cx) < 0.85f && fabsf(G.pl.y - cy) < 0.85f) return true;
    for (int i = 0; i < G.nmobs; i++) {
        const Mob *m = &G.mobs[i];
        if (m->state >= MS_DYING) continue;
        float r = 0.5f + g_mob_info[m->type].radius;
        if (fabsf(m->x - cx) < r && fabsf(m->y - cy) < r) return true;
    }
    return false;
}

static void door_open(Door *d)
{
    if (d->state == DOOR_CLOSED || d->state == DOOR_CLOSING) {
        if (d->state == DOOR_CLOSED) snd_play_at(SND_DOOR_OPEN, d->x + 0.5f, d->y + 0.5f, 1);
        d->state = DOOR_OPENING;
    } else if (d->state == DOOR_OPEN) {
        d->timer = 4.0f;
    }
}

static void doors_update(float dt)
{
    for (int i = 0; i < G.ndoors; i++) {
        Door *d = &G.doors[i];
        switch (d->state) {
        case DOOR_OPENING:
            d->open += dt * 1.7f;
            if (d->open >= 1) { d->open = 1; d->state = DOOR_OPEN; d->timer = 4.0f; }
            break;
        case DOOR_OPEN:
            d->timer -= dt;
            if (d->timer <= 0) {
                if (door_occupied(d)) d->timer = 0.5f;
                else { d->state = DOOR_CLOSING; snd_play_at(SND_DOOR_CLOSE, d->x + 0.5f, d->y + 0.5f, 0.8f); }
            }
            break;
        case DOOR_CLOSING:
            if (door_occupied(d)) { d->state = DOOR_OPENING; break; }
            d->open -= dt * 1.7f;
            if (d->open <= 0) { d->open = 0; d->state = DOOR_CLOSED; }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* flow field towards the player (monster path finding)                */
/* ------------------------------------------------------------------ */
static bool flow_pass(int x, int y)
{
    Cell *c = map_cell(x, y);
    if (!c || c->wall || (c->flags & CF_BLOCK)) return false;
    if (c->door >= 0) {
        const Door *d = &G.doors[c->door];
        if (d->lock != LOCK_NONE && d->open < 0.9f) return false;
    }
    return true;
}

static void flow_update(void)
{
    static int queue[MAP_MAX * MAP_MAX];
    for (int i = 0; i < MAP_MAX * MAP_MAX; i++) G.flow[i] = -1;
    int px = (int)G.pl.x, py = (int)G.pl.y;
    if (!map_cell(px, py)) return;
    int head = 0, tail = 0;
    G.flow[py * MAP_MAX + px] = 0;
    queue[tail++] = py * MAP_MAX + px;
    static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (head < tail) {
        int cur = queue[head++];
        int cx = cur % MAP_MAX, cy = cur / MAP_MAX;
        for (int k = 0; k < 4; k++) {
            int nx = cx + nb[k][0], ny = cy + nb[k][1];
            if (!flow_pass(nx, ny)) continue;
            int ni = ny * MAP_MAX + nx;
            if (G.flow[ni] >= 0) continue;
            G.flow[ni] = (i16)(G.flow[cur] + 1);
            queue[tail++] = ni;
        }
    }
}

static int flow_at(int x, int y)
{
    if (x < 0 || y < 0 || x >= G.w || y >= G.h) return -1;
    return G.flow[y * MAP_MAX + x];
}

/* pick a heading for a monster following the flow field */
static bool flow_dir(const Mob *m, float *ox, float *oy, int *tx, int *ty)
{
    int cx = (int)m->x, cy = (int)m->y;
    int here = flow_at(cx, cy);
    if (here < 0) return false;
    int best = here, bx = cx, by = cy;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int f = flow_at(cx + dx, cy + dy);
            if (f < 0) continue;
            if (dx && dy && (flow_at(cx + dx, cy) < 0 || flow_at(cx, cy + dy) < 0)) continue;
            if (f < best) { best = f; bx = cx + dx; by = cy + dy; }
        }
    float gx = bx + 0.5f, gy = by + 0.5f;
    if (best == here) { gx = G.pl.x; gy = G.pl.y; }
    float vx = gx - m->x, vy = gy - m->y, l = hypotf(vx, vy);
    if (l < 1e-4f) return false;
    *ox = vx / l; *oy = vy / l;
    *tx = bx; *ty = by;
    return true;
}

/* ------------------------------------------------------------------ */
/* combat                                                              */
/* ------------------------------------------------------------------ */
static void player_damage(float dmg)
{
    Player *p = &G.pl;
    if (p->dead || G.attract) return;
    p->hp -= dmg;
    p->hurt = minf(1.0f, p->hurt + dmg / 35.0f + 0.15f);
    if (p->hp <= 0) {
        p->hp = 0;
        p->dead = true;
        p->dead_t = 0;
        snd_play(SND_PLAYER_DIE, 1, 0, 1);
    } else if (s_hurt_snd_cd <= 0) {
        snd_play(SND_PLAYER_HURT, 0.9f, 0, frand2(0.9f, 1.1f));
        s_hurt_snd_cd = 0.35f;
    }
}

static void alert_mob(Mob *m)
{
    if (m->state != MS_IDLE) return;
    m->state = MS_CHASE;
    m->t = 0;
    m->cooldown = frand2(0.3f, 1.0f);
    const MobInfo *mi = &g_mob_info[m->type];
    snd_play_at(mi->snd_sight, m->x, m->y, mi->boss ? 1.5f : 1.0f);
    if (mi->boss) game_message("ABADDON AWAKENS!");
}

static void alert_noise(int radius)
{
    for (int i = 0; i < G.nmobs; i++) {
        Mob *m = &G.mobs[i];
        if (m->state != MS_IDLE) continue;
        int f = flow_at((int)m->x, (int)m->y);
        if (f >= 0 && f <= radius) alert_mob(m);
    }
}

static void maybe_drop(const Mob *m)
{
    float r = frand();
    int kind = -1;
    if (m->type == MOB_SKELETON && r < 0.45f) kind = TH_BOLTS;
    else if (m->type == MOB_GHOUL && r < 0.2f) kind = G.pl.has[W_BLUNDER] ? TH_SHOT : TH_BOLTS;
    else if (m->type == MOB_WRAITH && r < 0.35f) kind = TH_POTION;
    if (kind < 0) return;
    spawn_thing(kind, m->x, m->y);
    G.things[G.nthings - 1].drop = true;
}

static void damage_mob(int idx, float dmg, float ang)
{
    Mob *m = &G.mobs[idx];
    const MobInfo *mi = &g_mob_info[m->type];
    if (m->state >= MS_DYING) return;
    m->hp -= dmg;
    m->flash = 0.08f;
    float hz = m->z + (mi->flying ? 0.2f : 0.7f) * (mi->boss ? 1.8f : 1.0f);
    if (m->type == MOB_WRAITH) {
        for (int i = 0; i < 6; i++)
            spawn_particle(m->x, m->y, hz, frand2(-1, 1), frand2(-1, 1), frand2(0, 1), 0.5f,
                           pal_index(R_SPECTRE, irand(13, 15)), 1, false, true);
    } else if (m->type == MOB_SKELETON) {
        for (int i = 0; i < 5; i++)
            spawn_particle(m->x, m->y, hz, cosf(ang) * frand2(0.5f, 2), sinf(ang) * frand2(0.5f, 2), frand2(0.5f, 2.5f),
                           1.0f, pal_index(R_BONE, irand(8, 12)), 1, true, false);
    } else {
        blood_burst(m->x, m->y, hz, mi->boss ? 14 : 9, ang);
    }
    if (m->hp <= 0) {
        m->state = MS_DYING;
        m->t = 0;
        snd_play_at(mi->snd_die, m->x, m->y, mi->boss ? 1.6f : 1.0f);
        G.kills++;
        if (mi->boss) {
            G.boss_alive = false;
            game_message("ABADDON IS VANQUISHED. THE SEAL WEAKENS.");
        }
        if (m->type != MOB_WRAITH && m->type != MOB_BAT) blood_burst(m->x, m->y, 0.4f, 12, ang);
        maybe_drop(m);
        return;
    }
    if (m->state == MS_IDLE) alert_mob(m);
    if (frand() < mi->pain_chance && m->state != MS_PAIN) {
        m->state = MS_PAIN;
        m->t = 0;
        snd_play_at(mi->snd_pain, m->x, m->y, 0.9f);
    }
}

static float wall_dist(float x, float y, float dx, float dy, float *hx, float *hy)
{
    float t = 0;
    while (t < 48) {
        t += 0.04f;
        float px = x + dx * t, py = y + dy * t;
        if (map_solid((int)floorf(px), (int)floorf(py))) {
            *hx = x + dx * (t - 0.04f);
            *hy = y + dy * (t - 0.04f);
            return t;
        }
    }
    *hx = x + dx * t; *hy = y + dy * t;
    return t;
}

static void hitscan(float ang, float dmg)
{
    Player *p = &G.pl;
    float dx = cosf(ang), dy = sinf(ang), hx, hy;
    float bt = wall_dist(p->x, p->y, dx, dy, &hx, &hy);
    int best = -1;
    for (int i = 0; i < G.nmobs; i++) {
        const Mob *m = &G.mobs[i];
        if (m->state >= MS_DYING) continue;
        float ex = m->x - p->x, ey = m->y - p->y;
        float t = ex * dx + ey * dy;
        if (t <= 0 || t >= bt) continue;
        float perp = fabsf(ex * dy - ey * dx);
        if (perp < g_mob_info[m->type].radius + 0.06f) { best = i; bt = t; }
    }
    if (best >= 0) {
        damage_mob(best, dmg, ang);
        snd_play_at(SND_FLESH, G.mobs[best].x, G.mobs[best].y, 0.6f);
    } else {
        for (int i = 0; i < 5; i++)
            spawn_particle(hx, hy, p->eye + frand2(-0.15f, 0.1f), frand2(-0.5f, 0.5f), frand2(-0.5f, 0.5f), frand2(0.1f, 0.8f),
                           frand2(0.25f, 0.5f), pal_index(R_GRAY, irand(6, 10)), 1, false, false);
        spawn_particle(hx, hy, p->eye, 0, 0, 0, 0.08f, pal_index(R_FIRE, 15), 2, false, true);
    }
}

/* ------------------------------------------------------------------ */
/* player                                                              */
/* ------------------------------------------------------------------ */
static void player_use(void)
{
    Player *p = &G.pl;
    float dx = cosf(p->ang), dy = sinf(p->ang);
    for (float d = 0.2f; d < 1.6f; d += 0.05f) {
        int cx = (int)floorf(p->x + dx * d), cy = (int)floorf(p->y + dy * d);
        Cell *c = map_cell(cx, cy);
        if (!c) return;
        if (c->door >= 0) {
            Door *door = &G.doors[c->door];
            if (door->state == DOOR_OPEN || door->state == DOOR_OPENING) {
                if (door->open > 0.9f) continue;
                return;
            }
            if (door->lock == LOCK_RED && !p->key_red) {
                game_message("SEALED BY A CRIMSON SIGIL. FIND THE BLOOD KEY.");
                snd_play(SND_LOCKED, 1, 0, 1);
            } else if (door->lock == LOCK_GOLD && !p->key_gold) {
                game_message("SEALED BY A GOLDEN SIGIL. FIND THE GILDED KEY.");
                snd_play(SND_LOCKED, 1, 0, 1);
            } else {
                door_open(door);
            }
            return;
        }
        if (c->wall) {
            if (c->wall == WT_EXIT + 1) {
                if (g_levels[G.level].need_boss && G.boss_alive) {
                    game_message("THE SEAL HOLDS WHILE ABADDON LIVES.");
                    snd_play(SND_LOCKED, 1, 0, 0.7f);
                } else {
                    G.exit_reached = true;
                    snd_play(SND_EXIT, 1, 0, 1);
                }
            }
            return;
        }
    }
}

static void pickups(void)
{
    Player *p = &G.pl;
    for (int i = 0; i < G.nthings; i++) {
        Thing *t = &G.things[i];
        if (!t->alive || !g_thing_info[t->kind].pickup) continue;
        if (sqr(t->x - p->x) + sqr(t->y - p->y) > sqr(0.6f)) continue;
        int snd = SND_PICKUP;
        switch (t->kind) {
        case TH_POTION:
            if (p->hp >= 100) continue;
            p->hp = minf(100, p->hp + 25);
            game_message("A HEALING DRAUGHT");
            break;
        case TH_CHALICE:
            if (p->hp >= 150) continue;
            p->hp = minf(150, p->hp + 50);
            game_message("THE BLOOD CHALICE! +50");
            break;
        case TH_BOLTS:
            if (p->bolts >= 99) continue;
            p->bolts = clampi(p->bolts + 12, 0, 99);
            game_message("SILVER BOLTS");
            snd = SND_AMMO;
            break;
        case TH_SHOT:
            if (p->shot >= 50) continue;
            p->shot = clampi(p->shot + 8, 0, 50);
            game_message("A POUCH OF LEAD SHOT");
            snd = SND_AMMO;
            break;
        case TH_BLUNDER:
            if (!p->has[W_BLUNDER]) p->want_weapon = W_BLUNDER;
            p->has[W_BLUNDER] = true;
            p->shot = clampi(p->shot + 12, 0, 50);
            game_message("THE BLUNDERBUSS! PRESS 3");
            snd = SND_WEAPON;
            break;
        case TH_KEY_RED:
            p->key_red = true;
            game_message("THE BLOOD KEY");
            snd = SND_KEY;
            break;
        case TH_KEY_GOLD:
            p->key_gold = true;
            game_message("THE GILDED KEY");
            snd = SND_KEY;
            break;
        }
        t->alive = false;
        if (!t->drop) G.items++;
        p->bonus = 0.6f;
        snd_play(snd, 1, 0, 1);
        if (g_thing_info[t->kind].power > 0) lights_rebuild();
    }
}

static bool weapon_ready(const Player *p, int w)
{
    if (!p->has[w]) return false;
    if (w == W_CROSSBOW) return p->bolts > 0;
    if (w == W_BLUNDER) return p->shot > 0;
    return true;
}

static void fire_weapon(Player *p)
{
    switch (p->weapon) {
    case W_DAGGER: {
        p->wtimer = 0.42f;
        snd_play(SND_DAGGER, 0.8f, 0, frand2(0.9f, 1.1f));
        float best = 1.35f;
        int bi = -1;
        for (int i = 0; i < G.nmobs; i++) {
            const Mob *m = &G.mobs[i];
            if (m->state >= MS_DYING) continue;
            float dx = m->x - p->x, dy = m->y - p->y, d = hypotf(dx, dy) - g_mob_info[m->type].radius;
            if (d > best) continue;
            if (fabsf(wrap_angle(atan2f(dy, dx) - p->ang)) > 0.6f) continue;
            best = d;
            bi = i;
        }
        if (bi >= 0) {
            damage_mob(bi, frand2(20, 35), p->ang);
            snd_play(SND_DAGGER_HIT, 0.9f, 0, 1);
        }
        break;
    }
    case W_CROSSBOW:
        p->bolts--;
        p->wtimer = 0.75f;
        snd_play(SND_CROSSBOW, 0.9f, 0, frand2(0.95f, 1.05f));
        hitscan(p->ang + frand2(-0.008f, 0.008f), frand2(32, 48));
        alert_noise(9);
        break;
    case W_BLUNDER:
        p->shot--;
        p->wtimer = 1.05f;
        snd_play(SND_BLUNDER, 1, 0, frand2(0.95f, 1.05f));
        for (int i = 0; i < 9; i++) hitscan(p->ang + frand2(-0.11f, 0.11f), frand2(7, 13));
        s_muzzle = 1.4f;
        p->pitch = clampf(p->pitch + 0.06f, -1, 1);
        alert_noise(20);
        break;
    }
    p->wanim = 0;
}

static void player_update(float dt)
{
    Player *p = &G.pl;
    if (p->dead) {
        p->dead_t += dt;
        p->eye = maxf(0.12f, p->eye - dt * 0.9f);
        return;
    }
    float turn = 0;
    if (g_in.down[K_LEFT]) turn -= 1;
    if (g_in.down[K_RIGHT]) turn += 1;
    p->ang = wrap_angle(p->ang + turn * 2.5f * dt + g_in.mdx * 0.0022f * g_sens);
    p->pitch = clampf(p->pitch - g_in.mdy * 0.0035f * g_sens, -1, 1);

    float fw = 0, st = 0;
    if (g_in.down[K_W] || g_in.down[K_UP]) fw += 1;
    if (g_in.down[K_S] || g_in.down[K_DOWN]) fw -= 1;
    if (g_in.down[K_A]) st -= 1;
    if (g_in.down[K_D]) st += 1;
    float l = hypotf(fw, st);
    if (l > 1) { fw /= l; st /= l; }
    float speed = g_in.down[K_SHIFT] ? 5.4f : 3.6f;
    float dx = cosf(p->ang), dy = sinf(p->ang), rx = -dy, ry = dx;
    float tvx = (dx * fw + rx * st) * speed, tvy = (dy * fw + ry * st) * speed;
    float k = minf(1.0f, dt * 12.0f);
    p->vx += (tvx - p->vx) * k;
    p->vy += (tvy - p->vy) * k;
    move_body(&p->x, &p->y, p->vx * dt, p->vy * dt, 0.25f, -1);
    float sp = hypotf(p->vx, p->vy);
    p->bob += dt * (6.0f + sp * 1.2f);
    p->bob_amt += (minf(sp / 3.6f, 1.3f) - p->bob_amt) * minf(1.0f, dt * 8);
    p->eye = 0.6f + sinf(p->bob * 2) * 0.025f * p->bob_amt;

    Cell *c = map_cell((int)p->x, (int)p->y);
    if (c) c->flags |= CF_SEEN;

    if (g_in.pressed[K_E] || g_in.pressed[K_SPACE] || g_in.mouse_pressed[3]) player_use();
    pickups();

    /* weapon selection */
    if (g_in.pressed[K_1]) p->want_weapon = W_DAGGER;
    if (g_in.pressed[K_2] && p->has[W_CROSSBOW]) p->want_weapon = W_CROSSBOW;
    if (g_in.pressed[K_3] && p->has[W_BLUNDER]) p->want_weapon = W_BLUNDER;
    if (g_in.wheel || g_in.pressed[K_Q]) {
        int dir = g_in.wheel > 0 ? -1 : 1;
        int w = p->want_weapon;
        for (int i = 0; i < W_COUNT; i++) {
            w = (w + dir + W_COUNT) % W_COUNT;
            if (weapon_ready(p, w)) break;
        }
        p->want_weapon = w;
    }
    if (p->want_weapon != p->weapon) {
        p->wswitch += dt * 6;
        if (p->wswitch >= 1) { p->wswitch = 1; p->weapon = p->want_weapon; }
    } else if (p->wswitch > 0) {
        p->wswitch = maxf(0, p->wswitch - dt * 6);
    }

    p->wtimer -= dt;
    p->wanim += dt;
    bool fire = g_in.mouse[1] || g_in.down[K_CTRL] || g_in.down[K_F];
    if (fire && p->wtimer <= 0 && p->wswitch <= 0 && p->weapon == p->want_weapon) {
        if (weapon_ready(p, p->weapon)) {
            fire_weapon(p);
        } else {
            snd_play(SND_DRYFIRE, 0.6f, 0, 1);
            p->wtimer = 0.4f;
            p->want_weapon = weapon_ready(p, W_BLUNDER) ? W_BLUNDER : weapon_ready(p, W_CROSSBOW) ? W_CROSSBOW : W_DAGGER;
        }
    }
}

/* ------------------------------------------------------------------ */
/* monsters                                                            */
/* ------------------------------------------------------------------ */
static void mob_ranged(Mob *m, float ang)
{
    const MobInfo *mi = &g_mob_info[m->type];
    float hz = m->type == MOB_DEMON ? 1.2f : m->type == MOB_WRAITH ? 0.8f : 0.7f;
    float fx = m->x + cosf(ang) * (mi->radius + 0.1f), fy = m->y + sinf(ang) * (mi->radius + 0.1f);
    snd_play_at(SND_FIREBALL, m->x, m->y, 0.9f);
    switch (mi->proj) {
    case PJ_FIREBALL: spawn_proj(PJ_FIREBALL, fx, fy, hz, ang + frand2(-0.04f, 0.04f), 7.0f, frand2(10, 16)); break;
    case PJ_SPECTRAL: spawn_proj(PJ_SPECTRAL, fx, fy, hz, ang + frand2(-0.03f, 0.03f), 6.0f, frand2(12, 18)); break;
    case PJ_HELLBALL: {
        int n = m->hp < mi->hp * 0.5f ? 5 : 3;
        for (int i = 0; i < n; i++)
            spawn_proj(PJ_HELLBALL, fx, fy, hz, ang + (i - (n - 1) * 0.5f) * 0.16f, 8.5f, frand2(16, 24));
        break;
    }
    }
}

static void mob_update(int idx, float dt)
{
    Mob *m = &G.mobs[idx];
    const MobInfo *mi = &g_mob_info[m->type];
    Player *p = &G.pl;
    m->t += dt;
    m->anim += dt;
    m->cooldown -= dt;
    m->flash -= dt;
    float dx = p->x - m->x, dy = p->y - m->y, dist = hypotf(dx, dy);
    float ang = atan2f(dy, dx);

    if (mi->flying && m->state < MS_DYING) m->z = mi->z + sinf(m->anim * 2.5f) * 0.08f;

    switch (m->state) {
    case MS_IDLE:
        m->sight_cd -= dt;
        if (m->sight_cd <= 0) {
            m->sight_cd = 0.25f;
            if (!p->dead && dist < 20 && los(m->x, m->y, p->x, p->y)) alert_mob(m);
        }
        break;
    case MS_CHASE: {
        if (p->dead) break;
        bool see = dist < 24 && los(m->x, m->y, p->x, p->y);
        if (see && dist < mi->melee_range + mi->radius && m->cooldown <= 0) {
            m->state = MS_MELEE; m->t = 0; m->fired = false;
            if (m->type == MOB_GHOUL) snd_play_at(SND_GHOUL_ATTACK, m->x, m->y, 1);
            else if (m->type == MOB_BAT) snd_play_at(SND_BAT, m->x, m->y, 0.7f);
            break;
        }
        if (mi->proj >= 0 && see && dist > 2.0f && dist < 20 && m->cooldown <= 0 && frand() < mi->ranged_rate * dt * 3) {
            m->state = MS_RANGED; m->t = 0; m->fired = false;
            break;
        }
        float vx, vy;
        int tx = -1, ty = -1;
        if (see && dist < 5) { vx = dx / dist; vy = dy / dist; }
        else if (!flow_dir(m, &vx, &vy, &tx, &ty)) {
            if (!see) break;
            vx = dx / dist; vy = dy / dist;
        }
        if (tx >= 0) {
            Cell *c = map_cell(tx, ty);
            if (c && c->door >= 0 && G.doors[c->door].lock == LOCK_NONE) door_open(&G.doors[c->door]);
        }
        float side = 0;
        if (m->type == MOB_WRAITH || m->type == MOB_BAT) side = sinf(m->anim * (m->type == MOB_BAT ? 5.0f : 1.3f)) * 0.7f;
        /* stuck >= 0 accumulates time spent blocked; stuck < 0 is an evasive sidestep */
        if (m->stuck < 0) side += m->strafe * 1.5f;
        float mvx = vx - vy * side, mvy = vy + vx * side;
        float ml = hypotf(mvx, mvy);
        mvx /= ml; mvy /= ml;
        float spd = mi->speed;
        if (see && dist < mi->melee_range + mi->radius * 0.5f) spd = 0;
        float ox = m->x, oy = m->y;
        move_body(&m->x, &m->y, mvx * spd * dt, mvy * spd * dt, mi->radius, idx);
        float moved = hypotf(m->x - ox, m->y - oy);
        if (m->stuck < 0) {
            m->stuck = minf(0, m->stuck + dt);
        } else if (spd > 0 && moved < spd * dt * 0.3f) {
            m->stuck += dt;
            if (m->stuck > 0.5f) { m->stuck = -0.5f; m->strafe = -m->strafe; }
        } else {
            m->stuck = maxf(0, m->stuck - dt);
        }
        if (m->type == MOB_BAT && frand() < dt * 0.3f) snd_play_at(SND_BAT, m->x, m->y, 0.5f);
        break;
    }
    case MS_MELEE:
        if (m->t > 0.3f && !m->fired) {
            m->fired = true;
            if (dist < mi->melee_range + mi->radius + 0.4f && los(m->x, m->y, p->x, p->y))
                player_damage(frand2(mi->melee_min, mi->melee_max));
        }
        if (m->t > 0.6f) { m->state = MS_CHASE; m->t = 0; m->cooldown = mi->attack_cd * frand2(0.7f, 1.2f); }
        break;
    case MS_RANGED:
        if (m->t > 0.45f && !m->fired) { m->fired = true; mob_ranged(m, ang); }
        if (m->t > 0.8f) { m->state = MS_CHASE; m->t = 0; m->cooldown = mi->attack_cd * frand2(0.7f, 1.3f); }
        break;
    case MS_PAIN:
        if (m->t > 0.3f) { m->state = MS_CHASE; m->t = 0; }
        break;
    case MS_DYING:
        if (mi->flying) m->z = maxf(0, m->z - dt * 1.5f);
        if (m->t > DIE_TIME) { m->state = MS_DEAD; m->t = 0; }
        break;
    case MS_DEAD:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* projectiles, particles, effects                                     */
/* ------------------------------------------------------------------ */
static void proj_explode(Proj *p)
{
    p->alive = false;
    spawn_blast(p->x, p->y, p->z);
    snd_play_at(SND_EXPLODE, p->x, p->y, p->type == PJ_HELLBALL ? 1.0f : 0.7f);
    int ramp = p->type == PJ_SPECTRAL ? R_SPECTRE : R_FIRE;
    for (int i = 0; i < 10; i++)
        spawn_particle(p->x, p->y, p->z, frand2(-2, 2), frand2(-2, 2), frand2(-1, 2), frand2(0.2f, 0.5f),
                       pal_index(ramp, irand(13, 15)), 1, true, true);
}

static void proj_update(float dt)
{
    Player *pl = &G.pl;
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &G.proj[i];
        if (!p->alive) continue;
        p->life -= dt;
        if (p->life <= 0) { p->alive = false; continue; }
        if (p->type == PJ_SPECTRAL && !pl->dead) {
            float want = atan2f(pl->y - p->y, pl->x - p->x), cur = atan2f(p->vy, p->vx);
            float d = clampf(wrap_angle(want - cur), -1.2f * dt, 1.2f * dt);
            float sp = hypotf(p->vx, p->vy);
            p->vx = cosf(cur + d) * sp;
            p->vy = sinf(cur + d) * sp;
        }
        int n = 4;
        for (int s = 0; s < n && p->alive; s++) {
            p->x += p->vx * dt / n;
            p->y += p->vy * dt / n;
            if (map_solid((int)floorf(p->x), (int)floorf(p->y))) {
                p->x -= p->vx * dt / n;
                p->y -= p->vy * dt / n;
                proj_explode(p);
                break;
            }
            if (!pl->dead && sqr(p->x - pl->x) + sqr(p->y - pl->y) < sqr(0.3f + (p->type == PJ_HELLBALL ? 0.15f : 0.08f))) {
                player_damage(p->dmg);
                proj_explode(p);
            }
        }
        if (p->alive && frand() < dt * 30)
            spawn_particle(p->x, p->y, p->z + frand2(-0.05f, 0.05f), 0, 0, 0.3f, 0.3f,
                           p->type == PJ_SPECTRAL ? pal_index(R_PURPLE, 13) : pal_index(R_FIRE, 13), 1, false, true);
    }
}

static void particles_update(float dt)
{
    for (int i = 0; i < G.npart;) {
        Particle *p = &G.parts[i];
        p->life -= dt;
        if (p->life <= 0) { G.parts[i] = G.parts[--G.npart]; continue; }
        if (p->grav) p->vz -= 9.0f * dt;
        float nx = p->x + p->vx * dt, ny = p->y + p->vy * dt;
        if (map_solid((int)floorf(nx), (int)floorf(ny))) { p->vx = p->vy = 0; }
        else { p->x = nx; p->y = ny; }
        p->z += p->vz * dt;
        if (p->z < 0.01f) {
            p->z = 0.01f;
            p->vz = 0;
            p->vx *= 0.3f;
            p->vy *= 0.3f;
            if (p->grav && !p->glow && p->life > 2.0f) p->life = 2.0f;
            if (p->grav && !p->glow) p->life += dt * 0.7f; /* blood lingers */
        }
        i++;
    }
}

static void separate_mobs(void)
{
    for (int i = 0; i < G.nmobs; i++) {
        Mob *a = &G.mobs[i];
        if (a->state >= MS_DYING) continue;
        for (int j = i + 1; j < G.nmobs; j++) {
            Mob *b = &G.mobs[j];
            if (b->state >= MS_DYING) continue;
            float r = (g_mob_info[a->type].radius + g_mob_info[b->type].radius) * 0.9f;
            float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy;
            if (d2 >= r * r || d2 < 1e-8f) continue;
            float d = sqrtf(d2), push = (r - d) * 0.5f;
            dx /= d; dy /= d;
            if (!blocked(a->x - dx * push, a->y - dy * push, g_mob_info[a->type].radius)) { a->x -= dx * push; a->y -= dy * push; }
            if (!blocked(b->x + dx * push, b->y + dy * push, g_mob_info[b->type].radius)) { b->x += dx * push; b->y += dy * push; }
        }
    }
}

void loadout_from_player(Loadout *l)
{
    const Player *p = &G.pl;
    l->hp = maxf(p->hp, 50);
    l->bolts = p->bolts;
    l->shot = p->shot;
    memcpy(l->has, p->has, sizeof l->has);
    l->weapon = p->weapon;
}

void game_update(float dt)
{
    Player *p = &G.pl;
    if (!p->dead && !G.exit_reached) G.time += dt;
    for (int i = 0; i < G.nthings; i++) G.things[i].phase += dt;
    s_muzzle = maxf(0, s_muzzle - dt * 9);
    s_hurt_snd_cd -= dt;
    p->hurt = maxf(0, p->hurt - dt * 1.6f);
    p->bonus = maxf(0, p->bonus - dt * 1.5f);
    G.msg_t -= dt;
    G.title_t -= dt;

    if (g_levels[G.level].rain || g_levels[G.level].sky == 1) {
        G.lightning_cd -= dt;
        if (G.lightning_cd <= 0) {
            G.lightning = 1.0f;
            G.lightning_cd = frand2(9, 22);
            snd_play(SND_THUNDER, frand2(0.6f, 1.0f), frand2(-0.5f, 0.5f), frand2(0.8f, 1.1f));
        }
        float prev = G.lightning;
        G.lightning = maxf(0, G.lightning - dt * 2.6f);
        if (prev > 0.55f && G.lightning <= 0.55f && frand() < 0.6f) G.lightning = 0.9f;
    }

    if (G.attract) {
        p->ang = wrap_angle(p->ang + dt * 0.12f);
        p->eye = 0.6f;
        lights_update();
        snd_listener(p->x, p->y, p->ang);
        return;
    }

    player_update(dt);
    doors_update(dt);
    G.flow_timer -= dt;
    if (G.flow_timer <= 0) { flow_update(); G.flow_timer = 0.2f; }
    for (int i = 0; i < G.nmobs; i++) mob_update(i, dt);
    separate_mobs();
    proj_update(dt);
    particles_update(dt);
    for (int i = 0; i < 64; i++) {
        Blast *b = &G.blasts[i];
        if (b->life <= 0) continue;
        b->life -= dt;
        b->t += dt;
    }
    lights_update();
    snd_listener(p->x, p->y, p->ang);
}
