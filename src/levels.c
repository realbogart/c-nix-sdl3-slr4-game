/*
 * Level data. Each map is an ASCII grid:
 *
 * walls   # stone  % warm stone  W stained glass  P wood panel  M moss
 *         R blood  B ossuary     F banner          O organ       X exit seal
 * doors   D door   1 red-sealed  2 gold-sealed
 * decor   c candelabra  t brazier  L chandelier  p pillar  v gravestone
 *         x stone cross T dead tree k skull pile G cage    C coffin
 * items   + draught  * chalice  a bolts  h shot  S blunderbuss  r red key  y gold key
 * foes    g ghoul  s skeleton  w wraith  b bat  Z Abaddon
 *         @ player start
 */
#include "common.h"

static const char *L1[] = {
    "MMMMMMMMMMMMMMM#W##W##W##W##W###",
    "M..v...x...T...#c.............c#",
    "M..........v...#..p...p...p....#",
    "M.x..v....b....#..........g....#",
    "M......T......x#...L.......L...#",
    "M..v.........t.#.......s.......#",
    "M..............D...............#",
    "M.@..........t.#...L.......L...#",
    "M..x...v..g....#..p...p...p....#",
    "M......T...x...#....g..........#",
    "M.v...........v#c.....+......Sc#",
    "M...x....v.....########D########",
    "M..T.....b...T.BBBBBBBB.BBBBBBBB",
    "M.v...x.....+..BBBBBBc...cBBBBBB",
    "M......a.......BBBBBB.....BBBBBB",
    "MMMMMMMMMMMMMMMBBBBBB..g..BBBBBB",
    "RRRRRRRRRRRRRRRB.k.............B",
    "R.C...c.c...C.RB.BBBBB...BBBBB.B",
    "R.............RB.BBBBBB.BBBBBB.B",
    "F..p.......p..FB.B....B.B....B.B",
    "R.............RB.B.s..B.B..g.B.B",
    "R...w.....b...RB.D....B.B....D.B",
    "F..p.......p..FB.B.c.+B.B.+.cB.B",
    "R......+......RB.BBBBBB.BBBBBB.B",
    "X..............1...............B",
    "R.............RBBBBBBB...BBBBBBB",
    "F..p..b....p..FBBBBB.......BBBBB",
    "R.............RBBBBB.g.r.g.BBBBB",
    "R.C...c.c...C.RBBBBBc.....cBBBBB",
    "R.............RBBBBBBB.t.BBBBBBB",
    "RRRRRRRRRRRRRRRBBBBBBBBBBBBBBBBB",
};

static const int L1_REG[][7] = {
    {1, 1, 14, 14, FT_DIRT, FT_VAULT, 1},
    {16, 1, 30, 10, FT_MARBLE, FT_BEAMS, 0},
    {1, 17, 13, 29, FT_FLAG, FT_VAULT, 0},
    {1, 23, 14, 25, FT_CARPET, FT_VAULT, 0},
    {-1, 0, 0, 0, 0, 0, 0},
};

static const char *L2[] = {
    "###X############################",
    "#.....#WWWWOOOOOOOOOOWWWW#.....#",
    "#.c.c.#c................c#..y..#",
    "#.....#..................#.s.s.#",
    "###2###.....L......L.....###1###",
    "W.....#..p............p..#.....W",
    "#..g..#..................#.....#",
    "W..............................W",
    "#.....#..p.....w......p..#..s..#",
    "W.....#.....L......L.....#.....W",
    "#.+...#..................#...a.#",
    "W........p............p........W",
    "#.....#..................#.....#",
    "W..s..#.......g..........#..g..W",
    "#.....#..p..L......L..p..#.....#",
    "W..............................W",
    "#.....#........s.........#.....#",
    "W.c...#..p............p..#...c.W",
    "#.....#..................#.....#",
    "W..+..#c................c#..h..W",
    "###############DD###############",
    "#.c.c.#...t..........t...#.+.+.#",
    "#.....#..................#.....#",
    "#..r..D..x....v....x...v.D..g..#",
    "#.....#..................#.....#",
    "#.w...#...v..T.....T..v..#..b..#",
    "#.....#..................#.....#",
    "#######..x.....@......x..#######",
    "#######...+......a.......#######",
    "#######.....t......t.....#######",
    "################################",
};

static const int L2_REG[][7] = {
    {7, 21, 24, 29, FT_COBBLE, FT_VAULT, 1},
    {7, 2, 24, 19, FT_MARBLE, FT_VAULT, 0},
    {14, 2, 17, 19, FT_CARPET, FT_VAULT, 0},
    {1, 1, 5, 3, FT_WOOD, FT_BEAMS, 0},
    {26, 1, 30, 3, FT_WOOD, FT_BEAMS, 0},
    {-1, 0, 0, 0, 0, 0, 0},
};

static const char *L3[] = {
    "RRRRRRRRRRRRRRRXRRRRRRRRRRRRRRRR",
    "RRRRt......................tRRRR",
    "RRRR..p....p........p....p..RRRR",
    "RRRR........................RRRR",
    "RRRR........................RRRR",
    "RRRR..p.........Z........p..RRRR",
    "RRRR........................RRRR",
    "RRRRt......................tRRRR",
    "RRRR..p....p........p....p..RRRR",
    "RRRR....+..............+....RRRR",
    "RRRR........................RRRR",
    "RRRRRRRRRRRRRRR1RRRRRRRRRRRRRRRR",
    "BBBBBBBBBBBBBBB.BBBBBBBBBBBBBBBB",
    "Bc..cBk......kB.Bc............cB",
    "B....B........B.B..p...p...p...B",
    "B.r..D...g....B.B......s.......B",
    "B....B........B.B..............B",
    "Bc..cB..w.....B.B..p...p...p...B",
    "BBBBBB...+....B.B...g......a...B",
    "BBBBBBBBB.BBBBB.BBBBBBB.BBBBBBBB",
    "B.k..........b..........b...k..B",
    "BBBBBB.BBBBBBBB.BBBBBBBBB.BBBBBB",
    "B....g......BBB.BBB...s........B",
    "B..t.....t..B.....B..t......t..B",
    "B...........Bc...cB....g.......B",
    "B....+......B.....B..........h.B",
    "B...........B..@..B............B",
    "B..b...w....B.....B...s....+...B",
    "B..t.....t..Bc.a.cB..t......t..B",
    "B...........B.....B............B",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
};

static const int L3_REG[][7] = {
    {4, 1, 27, 10, FT_BLOODSTONE, FT_VAULT, 1},
    {1, 22, 11, 29, FT_DIRT, FT_VAULT, 0},
    {19, 22, 30, 29, FT_DIRT, FT_VAULT, 0},
    {17, 13, 30, 18, FT_BLOODSTONE, FT_VAULT, 0},
    {-1, 0, 0, 0, 0, 0, 0},
};

const LevelDef g_levels[NUM_LEVELS] = {
    {"CHAPTER I", "THE GRAVEYARD OF SAINT VITUS", 32, 31, L1, 0.0f, FT_FLAG, FT_VAULT, 0,
     SONG_GRAVEYARD, true, false, 0.07f, L1_REG},
    {"CHAPTER II", "THE CATHEDRAL OF ASH", 32, 31, L2, -PI / 2, FT_FLAG, FT_VAULT, 0,
     SONG_CATHEDRAL, true, false, 0.09f, L2_REG},
    {"CHAPTER III", "THE OSSUARY OF ABADDON", 32, 31, L3, -PI / 2, FT_FLAG, FT_VAULT, 1,
     SONG_OSSUARY, false, true, 0.06f, L3_REG},
};

static int wall_for(char c)
{
    switch (c) {
    case '#': return WT_STONE;
    case '%': return WT_STONE_WARM;
    case 'W': return WT_WINDOW;
    case 'P': return WT_WOOD;
    case 'M': return WT_MOSS;
    case 'R': return WT_BLOOD;
    case 'B': return WT_BONES;
    case 'F': return WT_BANNER;
    case 'O': return WT_ORGAN;
    case 'X': return WT_EXIT;
    default: return -1;
    }
}

static int thing_for(char c)
{
    switch (c) {
    case 'c': return TH_CANDLE;
    case 't': return TH_BRAZIER;
    case 'L': return TH_CHANDELIER;
    case 'p': return TH_PILLAR;
    case 'v': return TH_GRAVE;
    case 'x': return TH_CROSS;
    case 'T': return TH_TREE;
    case 'k': return TH_SKULLS;
    case 'C': return TH_COFFIN;
    case 'G': return TH_CAGE;
    case '+': return TH_POTION;
    case '*': return TH_CHALICE;
    case 'a': return TH_BOLTS;
    case 'h': return TH_SHOT;
    case 'S': return TH_BLUNDER;
    case 'r': return TH_KEY_RED;
    case 'y': return TH_KEY_GOLD;
    default: return -1;
    }
}

static int mob_for(char c)
{
    switch (c) {
    case 'g': return MOB_GHOUL;
    case 's': return MOB_SKELETON;
    case 'w': return MOB_WRAITH;
    case 'b': return MOB_BAT;
    case 'Z': return MOB_DEMON;
    default: return -1;
    }
}

bool level_load(int idx, const Loadout *carry)
{
    const LevelDef *L = &g_levels[idx];
    memset(&G, 0, sizeof G);
    G.level = idx;
    G.w = L->w;
    G.h = L->h;
    G.boss_idx = -1;
    if (G.w > MAP_MAX || G.h > MAP_MAX) { fprintf(stderr, "level %d too large\n", idx); return false; }

    for (int y = 0; y < G.h; y++) {
        if ((int)strlen(L->rows[y]) != G.w) {
            fprintf(stderr, "level %d row %d has length %zu, expected %d\n", idx, y, strlen(L->rows[y]), G.w);
            return false;
        }
        for (int x = 0; x < G.w; x++) {
            Cell *c = &G.cells[y * MAP_MAX + x];
            c->floor = (u8)L->def_floor;
            c->ceil = (u8)L->def_ceil;
            c->door = -1;
        }
    }
    for (const int *r = L->regions[0]; r[0] >= 0; r += 7)
        for (int y = r[1]; y <= r[3]; y++)
            for (int x = r[0]; x <= r[2]; x++) {
                Cell *c = &G.cells[y * MAP_MAX + x];
                c->floor = (u8)r[4];
                c->ceil = (u8)r[5];
                if (r[6]) c->flags |= CF_OUTDOOR; else c->flags &= (u8)~CF_OUTDOOR;
            }

    bool have_start = false;
    for (int y = 0; y < G.h; y++)
        for (int x = 0; x < G.w; x++) {
            char ch = L->rows[y][x];
            Cell *c = &G.cells[y * MAP_MAX + x];
            float cx = x + 0.5f, cy = y + 0.5f;
            int k;
            if ((k = wall_for(ch)) >= 0) {
                c->wall = (u8)(k + 1);
            } else if (ch == 'D' || ch == '1' || ch == '2') {
                if (G.ndoors >= MAX_DOORS) { fprintf(stderr, "too many doors\n"); return false; }
                Door *d = &G.doors[G.ndoors];
                d->x = x; d->y = y;
                d->lock = ch == '1' ? LOCK_RED : ch == '2' ? LOCK_GOLD : LOCK_NONE;
                d->tex = ch == '1' ? WT_DOOR_RED : ch == '2' ? WT_DOOR_GOLD : WT_DOOR;
                c->door = (i16)G.ndoors++;
            } else if ((k = thing_for(ch)) >= 0) {
                spawn_thing(k, cx, cy);
            } else if ((k = mob_for(ch)) >= 0) {
                spawn_mob(k, cx, cy);
            } else if (ch == '@') {
                G.pl.x = cx; G.pl.y = cy;
                have_start = true;
            } else if (ch != '.') {
                fprintf(stderr, "level %d: unknown map char '%c' at %d,%d\n", idx, ch, x, y);
                return false;
            }
        }
    if (!have_start) { fprintf(stderr, "level %d has no start\n", idx); return false; }

    for (int i = 0; i < G.ndoors; i++) {
        Door *d = &G.doors[i];
        bool wl = d->x > 0 && G.cells[d->y * MAP_MAX + d->x - 1].wall;
        bool wr = d->x < G.w - 1 && G.cells[d->y * MAP_MAX + d->x + 1].wall;
        bool dl = d->x > 0 && G.cells[d->y * MAP_MAX + d->x - 1].door >= 0;
        bool dr = d->x < G.w - 1 && G.cells[d->y * MAP_MAX + d->x + 1].door >= 0;
        d->axis = ((wl || dl) && (wr || dr)) ? 1 : 0;
    }

    for (int i = 0; i < G.nmobs; i++)
        if (G.mobs[i].type == MOB_DEMON) { G.boss_idx = i; G.boss_alive = true; }
    G.total_kills = G.nmobs;
    for (int i = 0; i < G.nthings; i++)
        if (g_thing_info[G.things[i].kind].pickup) G.total_items++;

    Player *p = &G.pl;
    p->ang = L->start_ang;
    p->eye = 0.6f;
    Loadout ld;
    if (carry) ld = *carry;
    else {
        memset(&ld, 0, sizeof ld);
        ld.hp = 100;
        ld.bolts = 30;
        ld.has[W_DAGGER] = ld.has[W_CROSSBOW] = true;
        ld.weapon = W_CROSSBOW;
    }
    G.start_load = ld;
    p->hp = ld.hp;
    p->bolts = ld.bolts;
    p->shot = ld.shot;
    memcpy(p->has, ld.has, sizeof p->has);
    p->weapon = p->want_weapon = ld.weapon;

    G.lightning_cd = 4.0f;
    G.title_t = 4.0f;
    lights_rebuild();
    return true;
}
