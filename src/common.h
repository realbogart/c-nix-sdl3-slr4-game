/*
 * NOCTIS - a retro gothic shooter in plain C.
 * Shared types, constants and module interfaces.
 */
#ifndef NOCTIS_COMMON_H
#define NOCTIS_COMMON_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t i16;
typedef int32_t i32;

/* Internal render resolution (scaled up to the window). */
#define SCR_W 480
#define SCR_H 270

#define PI 3.14159265f
#define TAU 6.28318531f

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float sqr(float a) { return a * a; }

/* ------------------------------------------------------------------ */
/* random (game.c)                                                     */
/* ------------------------------------------------------------------ */
u32 rng_next(void);
float frand(void);                  /* [0,1) */
float frand2(float lo, float hi);   /* [lo,hi) */
int irand(int lo, int hi);          /* [lo,hi] */
float wrap_angle(float a);          /* (-PI, PI] */

/* ------------------------------------------------------------------ */
/* platform: raw X11 protocol (x11.c)                                  */
/* ------------------------------------------------------------------ */
enum {
    K_NONE,
    K_UP, K_DOWN, K_LEFT, K_RIGHT,
    K_W, K_A, K_S, K_D, K_E, K_Q, K_F, K_M,
    K_1, K_2, K_3,
    K_SPACE, K_ENTER, K_ESC, K_TAB, K_SHIFT, K_CTRL, K_ALT,
    K_F11, K_PLUS, K_MINUS,
    K_COUNT
};

typedef struct {
    bool down[K_COUNT];
    bool pressed[K_COUNT];     /* went down this frame */
    bool mouse[4];             /* 1=left 2=middle 3=right */
    bool mouse_pressed[4];
    int wheel;                 /* +1 up / -1 down, accumulated this frame */
    float mdx, mdy;            /* relative mouse motion this frame (captured only) */
    bool quit;
    bool focused;
} Input;

extern Input g_in;

bool plat_init(const char *title, int w, int h);
void plat_poll(void);
void plat_present(const u32 *rgb, int w, int h);
void plat_capture_mouse(bool on);
bool plat_mouse_captured(void);
void plat_toggle_fullscreen(void);
void plat_shutdown(void);
double plat_time(void);
void plat_sleep(double seconds);

/* ------------------------------------------------------------------ */
/* audio: PulseAudio native protocol + software mixer (audio.c)        */
/* ------------------------------------------------------------------ */
enum {
    SND_DAGGER, SND_DAGGER_HIT, SND_CROSSBOW, SND_BLUNDER, SND_DRYFIRE,
    SND_PICKUP, SND_AMMO, SND_KEY, SND_WEAPON,
    SND_DOOR_OPEN, SND_DOOR_CLOSE, SND_LOCKED,
    SND_GHOUL_SIGHT, SND_GHOUL_ATTACK, SND_GHOUL_PAIN, SND_GHOUL_DIE,
    SND_SKEL_SIGHT, SND_SKEL_DIE,
    SND_WRAITH_SIGHT, SND_WRAITH_DIE,
    SND_BAT, SND_BAT_DIE,
    SND_DEMON_SIGHT, SND_DEMON_PAIN, SND_DEMON_DIE,
    SND_FIREBALL, SND_EXPLODE, SND_FLESH, SND_WALLHIT,
    SND_PLAYER_HURT, SND_PLAYER_DIE,
    SND_THUNDER, SND_BELL, SND_EXIT, SND_MENU,
    SND_COUNT
};

enum { SONG_TITLE, SONG_GRAVEYARD, SONG_CATHEDRAL, SONG_OSSUARY, SONG_VICTORY, SONG_COUNT };

#define AUDIO_RATE 48000

typedef struct { float *data; int len; } Sample;

bool audio_init(void);
void audio_update(void);
void audio_shutdown(void);
void snd_play(int id, float vol, float pan, float pitch);
void snd_play_at(int id, float x, float y, float vol);
void snd_listener(float x, float y, float ang);
void music_play(int song);
void music_set_enabled(bool on);
bool music_enabled(void);

void sfx_generate(Sample *out); /* sfx.c: fills SND_COUNT samples */

/* ------------------------------------------------------------------ */
/* art: palette, procedural textures and sprites, font (art.c)         */
/* ------------------------------------------------------------------ */
/* Images are stored column-major: px[x * h + y]. Index 255 = transparent. */
typedef struct { int w, h; u8 *px; } Image;

#define TRANSP 255
#define NLIGHT 32
#define LIGHT_ONE 24.0f /* colormap level that equals unlit texture colour */

extern u32 g_pal[256];
extern u8 g_cmap[NLIGHT][256];
extern u8 g_blend[256 * 256];
extern u8 g_addt[256 * 256];

enum {
    WT_STONE, WT_STONE_WARM, WT_WINDOW, WT_WOOD, WT_MOSS, WT_BLOOD, WT_BONES,
    WT_BANNER, WT_ORGAN, WT_DOOR, WT_DOOR_RED, WT_DOOR_GOLD, WT_JAMB, WT_EXIT,
    WT_COUNT
};

enum {
    FT_FLAG, FT_MARBLE, FT_WOOD, FT_DIRT, FT_CARPET, FT_COBBLE, FT_BLOODSTONE,
    FT_BEAMS, FT_VAULT,
    FT_COUNT
};

enum {
    SPR_CANDLE0, SPR_CANDLE1,
    SPR_BRAZIER0, SPR_BRAZIER1, SPR_BRAZIER2,
    SPR_CHANDELIER0, SPR_CHANDELIER1,
    SPR_PILLAR, SPR_GRAVE, SPR_CROSS, SPR_TREE, SPR_SKULLS, SPR_COFFIN, SPR_CAGE,
    SPR_POTION, SPR_CHALICE, SPR_BOLTS, SPR_SHOT, SPR_BLUNDER_PICK, SPR_KEY_RED, SPR_KEY_GOLD,
    SPR_GHOUL_WALK0, SPR_GHOUL_WALK1, SPR_GHOUL_ATTACK, SPR_GHOUL_PAIN,
    SPR_GHOUL_DIE0, SPR_GHOUL_DIE1, SPR_GHOUL_DEAD,
    SPR_SKEL_WALK0, SPR_SKEL_WALK1, SPR_SKEL_ATTACK0, SPR_SKEL_ATTACK1, SPR_SKEL_PAIN,
    SPR_SKEL_DIE0, SPR_SKEL_DEAD,
    SPR_WRAITH0, SPR_WRAITH1, SPR_WRAITH_ATTACK, SPR_WRAITH_PAIN, SPR_WRAITH_DIE0, SPR_WRAITH_DIE1,
    SPR_BAT0, SPR_BAT1, SPR_BAT_DEAD,
    SPR_DEMON_WALK0, SPR_DEMON_WALK1, SPR_DEMON_ATTACK, SPR_DEMON_PAIN,
    SPR_DEMON_DIE0, SPR_DEMON_DIE1, SPR_DEMON_DEAD,
    SPR_FIREBALL0, SPR_FIREBALL1, SPR_SPECTRAL0, SPR_SPECTRAL1, SPR_HELLBALL0, SPR_HELLBALL1,
    SPR_EXPLODE0, SPR_EXPLODE1, SPR_EXPLODE2,
    SPR_COUNT
};

enum {
    WPN_DAGGER_IDLE, WPN_DAGGER_SWING0, WPN_DAGGER_SWING1,
    WPN_XBOW_IDLE, WPN_XBOW_FIRED,
    WPN_BLUNDER_IDLE, WPN_BLUNDER_FIRE,
    WPN_FLASH,
    WPN_COUNT
};

extern Image g_walls[WT_COUNT];
extern Image g_flats[FT_COUNT];
extern Image g_spr[SPR_COUNT];
extern Image g_wpn[WPN_COUNT];
extern Image g_sky[2];

#define SKY_W 1024
#define SKY_H 240
#define SKY_HORIZON 170 /* sky row that sits on the horizon */

void art_init(void);
u8 pal_nearest(int r, int g, int b);
u8 pal_index(int ramp, int shade);

/* font: 5x7 glyphs */
void font_draw(u8 *fb, int x, int y, const char *s, int scale, u8 col);
void font_draw_shadow(u8 *fb, int x, int y, const char *s, int scale, u8 col);
int font_width(const char *s, int scale);

/* palette ramp ids (16 shades each) */
enum {
    R_GRAY, R_WARM, R_COOL, R_WOOD, R_BLOOD, R_FIRE, R_GOLD, R_BONE,
    R_MOSS, R_GHOUL, R_BLUE, R_PURPLE, R_CRIMSON, R_FLESH, R_SPECTRE, R_NIGHT
};

/* ------------------------------------------------------------------ */
/* world / game state (game.c, levels.c)                               */
/* ------------------------------------------------------------------ */
#define MAP_MAX 48
#define MAX_DOORS 64
#define MAX_LIGHTS 128
#define MAX_THINGS 512
#define MAX_MOBS 160
#define MAX_PROJ 128
#define MAX_PARTICLES 1024

enum { CF_OUTDOOR = 1, CF_BLOCK = 2, CF_SEEN = 4 };

typedef struct {
    u8 wall;    /* 0 = open, else WT_* + 1 */
    u8 floor;   /* FT_* */
    u8 ceil;    /* FT_* */
    u8 flags;
    i16 door;   /* -1 or index into doors */
} Cell;

enum { DOOR_CLOSED, DOOR_OPENING, DOOR_OPEN, DOOR_CLOSING };
enum { LOCK_NONE, LOCK_RED, LOCK_GOLD };

typedef struct {
    int x, y;
    int axis;    /* 0: door plane perpendicular to X (passage along X); 1: along Y */
    int state;
    int lock;
    float open;  /* 0 closed .. 1 open */
    float timer;
    int tex;
} Door;

typedef struct {
    float x, y;
    float power, radius;
    float flicker;   /* 0 = steady */
    float cur;       /* current flickered power */
    int ncells;
    int first;       /* index into light weight pool */
} Light;

/* Thing kinds: decorations and pickups */
enum {
    TH_CANDLE, TH_BRAZIER, TH_CHANDELIER, TH_PILLAR, TH_GRAVE, TH_CROSS, TH_TREE,
    TH_SKULLS, TH_COFFIN, TH_CAGE,
    TH_POTION, TH_CHALICE, TH_BOLTS, TH_SHOT, TH_BLUNDER, TH_KEY_RED, TH_KEY_GOLD,
    TH_COUNT
};

typedef struct {
    int kind;
    float x, y;
    bool alive;
    bool drop;      /* spawned by a dying monster, not counted in item stats */
    float phase;
} Thing;

enum { MOB_GHOUL, MOB_SKELETON, MOB_WRAITH, MOB_BAT, MOB_DEMON, MOB_COUNT };
enum { MS_IDLE, MS_CHASE, MS_MELEE, MS_RANGED, MS_PAIN, MS_DYING, MS_DEAD };

typedef struct {
    int type;
    float x, y, z;
    float hp;
    int state;
    float t;          /* time in current state */
    float cooldown;
    float anim;
    float sight_cd;
    bool fired;       /* attack resolved in current attack state */
    float stuck;
    float strafe;
    float flash;      /* hit flash timer */
} Mob;

enum { PJ_FIREBALL, PJ_SPECTRAL, PJ_HELLBALL };

typedef struct {
    int type;
    float x, y, z;
    float vx, vy;
    float dmg;
    float life;
    bool alive;
} Proj;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;
    u8 col;
    u8 size;   /* in 1/16 world units */
    u8 grav;
    u8 glow;
} Particle;

typedef struct {
    float x, y, z;
    float life;
    int frame;
    float t;
} Blast;

enum { W_DAGGER, W_CROSSBOW, W_BLUNDER, W_COUNT };

typedef struct {
    float x, y, ang, pitch;
    float vx, vy;
    float hp;
    int bolts, shot;
    bool has[W_COUNT];
    int weapon;
    int want_weapon;
    float wtimer;      /* weapon cooldown */
    float wanim;       /* animation timer since fire */
    float wswitch;     /* raise/lower animation, 0 = ready */
    bool key_red, key_gold;
    float bob;
    float bob_amt;
    float hurt;        /* red flash */
    float bonus;       /* gold flash */
    float dead_t;
    bool dead;
    float eye;         /* eye height */
} Player;

typedef struct {
    float hp;
    int bolts, shot;
    bool has[W_COUNT];
    int weapon;
} Loadout;

typedef struct {
    const char *name;
    const char *subtitle;
    int w, h;
    const char **rows;
    float start_ang;
    int def_floor, def_ceil;
    int sky;
    int song;
    bool rain;
    bool need_boss;
    float ambient;
    const int (*regions)[7];  /* x0,y0,x1,y1,floor,ceil,outdoor ; terminated by x0<0 */
} LevelDef;

#define NUM_LEVELS 3
extern const LevelDef g_levels[NUM_LEVELS];

typedef struct {
    int level;
    int w, h;
    Cell cells[MAP_MAX * MAP_MAX];
    Door doors[MAX_DOORS]; int ndoors;
    Light lights[MAX_LIGHTS]; int nlights;
    Thing things[MAX_THINGS]; int nthings;
    Mob mobs[MAX_MOBS]; int nmobs;
    Proj proj[MAX_PROJ];
    Particle parts[MAX_PARTICLES]; int npart;
    Blast blasts[64];
    float cell_light[MAP_MAX * MAP_MAX];
    float vert_light[(MAP_MAX + 1) * (MAP_MAX + 1)];
    i16 flow[MAP_MAX * MAP_MAX];
    float flow_timer;
    Player pl;
    Loadout start_load;
    int kills, total_kills, items, total_items;
    float time;
    float lightning, lightning_cd;
    float msg_t; char msg[96];
    float title_t;
    bool boss_alive; int boss_idx;
    bool exit_reached;
    bool attract;  /* title-screen demo: no AI, rotating camera */
} Game;

extern Game G;

/* light weights pool */
typedef struct { u16 cell; float w; } LightW;

typedef struct {
    int spr, nframes;
    float fps;
    bool block, ceiling, pickup;
    float power, radius, flicker;
} ThingInfo;

typedef struct {
    const char *name;
    float hp, speed, radius, scale, z;
    float melee_min, melee_max, melee_range;
    int proj;
    float ranged_rate, attack_cd, pain_chance;
    bool flying, translucent, boss;
    int snd_sight, snd_pain, snd_die;
    int spr_walk0, spr_walk1, spr_melee, spr_ranged0, spr_ranged1, spr_pain, spr_die0, spr_die1, spr_dead;
} MobInfo;

extern const ThingInfo g_thing_info[TH_COUNT];
extern const MobInfo g_mob_info[MOB_COUNT];
int mob_sprite(const Mob *m);
int thing_sprite(const Thing *t);

/* levels.c */
bool level_load(int idx, const Loadout *carry);

/* game.c */
extern float g_sens;
void game_update(float dt);
void game_message(const char *fmt, ...);
bool map_solid(int x, int y);           /* walls + closed doors */
bool map_blocks_move(int x, int y);     /* walls, closed doors, blocking things */
Cell *map_cell(int x, int y);
float light_at(float x, float y);
void lights_rebuild(void);
void loadout_from_player(Loadout *l);
void spawn_mob(int type, float x, float y);
void spawn_thing(int kind, float x, float y);

/* render.c */
extern u8 g_fb[SCR_W * SCR_H];
void render_world(void);
void render_weapon(void);
void render_hud(void);
void render_automap(void);
void render_darken(int level);
void render_to_rgb(u32 *out, float red, float gold, float white);
void render_fill(u8 col);
void render_box(int x, int y, int w, int h, u8 col);
void render_image(const Image *im, int x, int y, int lvl);

#endif
