/*
 * NOCTIS - main loop, menus and game flow.
 */
#include "common.h"

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_INTERMISSION, ST_VICTORY };

static int s_state = ST_TITLE;
static int s_menu;
static bool s_automap;
static float s_state_t;
static u32 s_rgb[SCR_W * SCR_H];
static bool s_running = true;
static Loadout s_carry;
static int s_start_level;

static u8 C(int ramp, int shade) { return pal_index(ramp, shade); }

static void text_c(const char *s, int y, int scale, u8 col)
{
    font_draw_shadow(g_fb, (SCR_W - font_width(s, scale)) / 2, y, s, scale, col);
}

static void enter_state(int st)
{
    s_state = st;
    s_menu = 0;
    s_state_t = 0;
    plat_capture_mouse(st == ST_PLAY && g_in.focused);
}

static void start_title(void)
{
    level_load(0, NULL);
    G.attract = true;
    G.pl.x = 7.5f;
    G.pl.y = 8.0f;
    G.pl.pitch = 0.08f;
    G.title_t = 0;
    music_play(SONG_TITLE);
    enter_state(ST_TITLE);
}

static void start_level(int idx, const Loadout *carry)
{
    if (!level_load(idx, carry)) { s_running = false; return; }
    music_play(g_levels[idx].song);
    snd_play(SND_BELL, 0.8f, 0, 1);
    s_automap = false;
    enter_state(ST_PLAY);
}

/* ------------------------------------------------------------------ */
/* menus                                                               */
/* ------------------------------------------------------------------ */
static int menu_nav(int n)
{
    if (g_in.pressed[K_UP] || g_in.pressed[K_W]) { s_menu = (s_menu + n - 1) % n; snd_play(SND_MENU, 0.6f, 0, 1); }
    if (g_in.pressed[K_DOWN] || g_in.pressed[K_S]) { s_menu = (s_menu + 1) % n; snd_play(SND_MENU, 0.6f, 0, 1); }
    if (g_in.pressed[K_ENTER] || g_in.pressed[K_SPACE] || g_in.mouse_pressed[1]) { snd_play(SND_MENU, 0.8f, 0, 0.8f); return s_menu; }
    return -1;
}

static void draw_menu(const char **items, int n, int y)
{
    for (int i = 0; i < n; i++) {
        bool sel = i == s_menu;
        char buf[64];
        snprintf(buf, sizeof buf, sel ? "+ %s +" : "%s", items[i]);
        text_c(buf, y + i * 20, 2, sel ? C(R_GOLD, 14) : C(R_BONE, 9));
    }
}

static void draw_title_logo(int y)
{
    const char *t = "NOCTIS";
    int sc = 8, w = font_width(t, sc), x = (SCR_W - w) / 2;
    font_draw(g_fb, x + 4, y + 5, t, sc, 0);
    font_draw(g_fb, x + 2, y + 2, t, sc, C(R_BLOOD, 3));
    font_draw(g_fb, x, y, t, sc, C(R_BLOOD, 9));
    for (int yy = y; yy < y + 7 * sc; yy++)
        for (int xx = x; xx < x + w; xx++) {
            u8 *p = &g_fb[yy * SCR_W + xx];
            if (*p == C(R_BLOOD, 9)) *p = C(R_BLOOD, yy < y + 2 * sc ? 11 : yy < y + 4 * sc ? 10 : 9);
        }
}

static void update_title(void)
{
    static const char *items[] = {"BEGIN THE VIGIL", "QUIT"};
    int sel = menu_nav(2);
    if (sel == 0) {
        start_level(s_start_level, NULL);
        return;
    }
    if (sel == 1 || g_in.pressed[K_ESC]) s_running = false;
    render_world();
    render_darken(17);
    draw_title_logo(36);
    text_c("A GOTHIC NIGHTMARE IN THREE CHAPTERS", 104, 1, C(R_BONE, 11));
    draw_menu(items, 2, 140);
    text_c("WASD MOVE   MOUSE / ARROWS LOOK   LMB / CTRL FIRE", SCR_H - 34, 1, C(R_BONE, 8));
    text_c("E / SPACE USE   1-3 WEAPON   TAB MAP   ESC MENU", SCR_H - 22, 1, C(R_BONE, 8));
}

static void update_pause(void)
{
    char music[32], sens[32];
    snprintf(music, sizeof music, "MUSIC: %s", music_enabled() ? "ON" : "OFF");
    snprintf(sens, sizeof sens, "MOUSE: %.1f", g_sens);
    const char *items[] = {"RESUME", "RESTART CHAPTER", music, sens, "QUIT TO TITLE", "QUIT GAME"};
    int sel = menu_nav(6);
    if (s_menu == 3 && (g_in.pressed[K_LEFT] || g_in.pressed[K_MINUS] || g_in.pressed[K_A])) g_sens = maxf(0.2f, g_sens - 0.1f);
    if (s_menu == 3 && (g_in.pressed[K_RIGHT] || g_in.pressed[K_PLUS] || g_in.pressed[K_D])) g_sens = minf(4.0f, g_sens + 0.1f);
    if (g_in.pressed[K_ESC] || sel == 0) { enter_state(ST_PLAY); return; }
    if (sel == 1) { Loadout l = G.start_load; start_level(G.level, &l); return; }
    if (sel == 2) music_set_enabled(!music_enabled());
    if (sel == 3) g_sens = g_sens >= 3.95f ? 0.2f : g_sens + 0.2f;
    if (sel == 4) { start_title(); return; }
    if (sel == 5) { s_running = false; return; }
    render_world();
    render_weapon();
    render_darken(9);
    text_c("PAUSED", 40, 4, C(R_BLOOD, 13));
    draw_menu(items, 6, 92);
}

static void format_time(char *buf, size_t n, float t)
{
    int s = (int)t;
    snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static void update_intermission(void)
{
    s_state_t += 1.0f / 60;
    render_world();
    render_darken(8);
    char buf[64];
    snprintf(buf, sizeof buf, "%s COMPLETE", g_levels[G.level].name);
    text_c(buf, 40, 3, C(R_GOLD, 14));
    text_c(g_levels[G.level].subtitle, 70, 1, C(R_BONE, 11));
    snprintf(buf, sizeof buf, "SLAIN   %d / %d", G.kills, G.total_kills);
    text_c(buf, 104, 2, C(R_BONE, 13));
    snprintf(buf, sizeof buf, "RELICS  %d / %d", G.items, G.total_items);
    text_c(buf, 128, 2, C(R_BONE, 13));
    char tb[16];
    format_time(tb, sizeof tb, G.time);
    snprintf(buf, sizeof buf, "TIME    %s", tb);
    text_c(buf, 152, 2, C(R_BONE, 13));
    if (s_state_t > 1.0f && ((int)(s_state_t * 2) & 1)) text_c("PRESS ENTER", 200, 1, C(R_GOLD, 13));
    if (s_state_t > 1.0f && (g_in.pressed[K_ENTER] || g_in.pressed[K_SPACE] || g_in.mouse_pressed[1])) {
        if (G.level + 1 < NUM_LEVELS) {
            loadout_from_player(&s_carry);
            start_level(G.level + 1, &s_carry);
        } else {
            music_play(SONG_VICTORY);
            enter_state(ST_VICTORY);
        }
    }
}

static void update_victory(void)
{
    s_state_t += 1.0f / 60;
    render_world();
    render_darken(6);
    static const char *lines[] = {
        "THE BEAST IS SLAIN.",
        "",
        "AS THE LAST BELL OF SAINT VITUS FALLS SILENT,",
        "A GREY DAWN CREEPS OVER THE GRAVES.",
        "THE DEAD SLEEP AGAIN - FOR NOW.",
        "",
        "THE VIGIL IS ENDED.",
    };
    text_c("DAWN", 30, 5, C(R_GOLD, 15));
    for (int i = 0; i < 7; i++) {
        if (s_state_t * 1.5f < i) break;
        text_c(lines[i], 90 + i * 14, 1, C(R_BONE, 12));
    }
    if (s_state_t > 5 && ((int)(s_state_t * 2) & 1)) text_c("PRESS ENTER", 220, 1, C(R_GOLD, 13));
    if (s_state_t > 2 && (g_in.pressed[K_ENTER] || g_in.pressed[K_ESC])) start_title();
}

static void update_play(float dt)
{
    if (g_in.pressed[K_ESC]) { enter_state(ST_PAUSE); return; }
    if (g_in.pressed[K_TAB]) s_automap = !s_automap;
    if (g_in.pressed[K_M]) music_set_enabled(!music_enabled());
    if (!plat_mouse_captured() && g_in.focused && g_in.mouse_pressed[1]) {
        plat_capture_mouse(true);
        g_in.mouse_pressed[1] = false;
        g_in.mouse[1] = false;
    }
    game_update(dt);
    if (G.exit_reached) { plat_capture_mouse(false); enter_state(ST_INTERMISSION); }

    render_world();
    render_weapon();
    if (s_automap) render_automap();
    render_hud();
    if (G.pl.dead) {
        float t = G.pl.dead_t;
        render_darken(clampi(24 - (int)(t * 10), 10, 24));
        if (t > 1.0f) {
            text_c("YOU HAVE PERISHED", 90, 3, C(R_BLOOD, 13));
            if (((int)(t * 2)) & 1) text_c("PRESS ENTER TO RISE AGAIN", 130, 1, C(R_BONE, 11));
            if (g_in.pressed[K_ENTER] || g_in.pressed[K_SPACE] || g_in.mouse_pressed[1]) {
                Loadout l = G.start_load;
                start_level(G.level, &l);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* headless screenshot mode (used for development / CI)                */
/* ------------------------------------------------------------------ */
/* check every chapter can be finished: start -> keys -> sealed doors -> exit */
static int validate_levels(void)
{
    int fails = 0;
    for (int l = 0; l < NUM_LEVELS; l++) {
        if (!level_load(l, NULL)) { fails++; continue; }
        bool red = false, gold = false, exit_ok = false;
        static u8 seen[MAP_MAX * MAP_MAX];
        static int q[MAP_MAX * MAP_MAX];
        int reach_mobs = 0;
        for (int pass = 0; pass < 4; pass++) {
            memset(seen, 0, sizeof seen);
            int h = 0, t = 0, s = (int)G.pl.y * MAP_MAX + (int)G.pl.x;
            seen[s] = 1;
            q[t++] = s;
            while (h < t) {
                int c = q[h++], cx = c % MAP_MAX, cy = c / MAP_MAX;
                static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (int k = 0; k < 4; k++) {
                    Cell *n = map_cell(cx + nb[k][0], cy + nb[k][1]);
                    int ni = (cy + nb[k][1]) * MAP_MAX + cx + nb[k][0];
                    if (!n || seen[ni]) continue;
                    if (n->wall == WT_EXIT + 1) exit_ok = true;
                    if (n->wall || (n->flags & CF_BLOCK)) continue;
                    if (n->door >= 0) {
                        int lk = G.doors[n->door].lock;
                        if ((lk == LOCK_RED && !red) || (lk == LOCK_GOLD && !gold)) continue;
                    }
                    seen[ni] = 1;
                    q[t++] = ni;
                }
            }
            for (int i = 0; i < G.nthings; i++) {
                const Thing *th = &G.things[i];
                if (!seen[(int)th->y * MAP_MAX + (int)th->x]) continue;
                if (th->kind == TH_KEY_RED) red = true;
                if (th->kind == TH_KEY_GOLD) gold = true;
            }
        }
        for (int i = 0; i < G.nmobs; i++) reach_mobs += seen[(int)G.mobs[i].y * MAP_MAX + (int)G.mobs[i].x];
        printf("level %d: exit %s, red key %s, gold key %s, %d/%d monsters reachable, %d doors, %d lights\n", l + 1,
               exit_ok ? "REACHABLE" : "UNREACHABLE", red ? "yes" : "no", gold ? "yes" : "no", reach_mobs, G.nmobs,
               G.ndoors, G.nlights);
        if (!exit_ok) fails++;
    }
    return fails ? 1 : 0;
}

static int write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        u8 px[3] = {(u8)(s_rgb[i] >> 16), (u8)(s_rgb[i] >> 8), (u8)s_rgb[i]};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static bool use_key;

static int run_shot(const char *path, int level, int frames, const float *pos, int weapon, bool title, bool map, int fire)
{
    if (title) {
        start_title();
        for (int i = 0; i < frames; i++) game_update(1.0f / 60);
        update_title();
    } else {
        if (!level_load(level, NULL)) return 1;
        if (pos) { G.pl.x = pos[0]; G.pl.y = pos[1]; G.pl.ang = pos[2]; }
        G.pl.has[W_BLUNDER] = true;
        G.pl.shot = 20;
        if (weapon >= 0) G.pl.weapon = G.pl.want_weapon = weapon;
        for (int i = 0; i < frames; i++) {
            memset(&g_in, 0, sizeof g_in);
            if (fire >= 0 && i == frames - fire) g_in.mouse[1] = true;
            if (use_key && i == 0) g_in.pressed[K_E] = true;
            game_update(1.0f / 60);
        }
        G.title_t = frames < 120 ? 2 : 0;
        render_world();
        render_weapon();
        if (map) render_automap();
        render_hud();
    }
    render_to_rgb(s_rgb, G.pl.hurt, G.pl.bonus, 0);
    return write_ppm(path);
}

int main(int argc, char **argv)
{
    const char *shot = NULL;
    int level = 0, frames = 1, weapon = -1, win_w = 960, win_h = 540, fire = -1;
    float pos[3];
    bool have_pos = false, title = false, map = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) level = clampi(atoi(argv[++i]) - 1, 0, NUM_LEVELS - 1);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--weapon") && i + 1 < argc) weapon = clampi(atoi(argv[++i]) - 1, 0, W_COUNT - 1);
        else if (!strcmp(argv[i], "--fire") && i + 1 < argc) fire = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--validate")) { art_init(); return validate_levels(); }
        else if (!strcmp(argv[i], "--title")) title = true;
        else if (!strcmp(argv[i], "--map")) map = true;
        else if (!strcmp(argv[i], "--use")) use_key = true;
        else if (!strcmp(argv[i], "--pos") && i + 3 < argc) {
            pos[0] = (float)atof(argv[++i]);
            pos[1] = (float)atof(argv[++i]);
            pos[2] = (float)atof(argv[++i]);
            have_pos = true;
        } else if (!strcmp(argv[i], "--window") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &win_w, &win_h) != 2) { win_w = 960; win_h = 540; }
        } else if (!strcmp(argv[i], "--mute")) music_set_enabled(false);
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("usage: noctis [--level N] [--window WxH] [--mute]\n"
                   "       noctis --shot out.ppm [--level N] [--frames K] [--pos X Y ANG] [--weapon N] [--fire K] [--map] [--title]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    art_init();
    if (shot) return run_shot(shot, level, frames, have_pos ? pos : NULL, weapon, title, map, fire);

    if (!plat_init("NOCTIS", win_w, win_h)) {
        fprintf(stderr, "noctis: could not open an X11 window (is DISPLAY set?)\n");
        return 1;
    }
    audio_init();
    s_start_level = level;
    start_title();

    double last = plat_time();
    while (s_running) {
        double now = plat_time();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        plat_poll();
        if (g_in.quit) break;
        if (g_in.pressed[K_F11] || (g_in.pressed[K_ENTER] && g_in.down[K_ALT])) {
            plat_toggle_fullscreen();
            g_in.pressed[K_ENTER] = false;
        }

        switch (s_state) {
        case ST_TITLE: game_update(dt); update_title(); break;
        case ST_PLAY: update_play(dt); break;
        case ST_PAUSE: update_pause(); break;
        case ST_INTERMISSION: update_intermission(); break;
        case ST_VICTORY: game_update(dt); update_victory(); break;
        }

        float white = G.lightning > 0.6f && (s_state == ST_PLAY || s_state == ST_TITLE) ? (G.lightning - 0.6f) * 0.25f : 0;
        Cell *pc = map_cell((int)G.pl.x, (int)G.pl.y);
        if (!pc || !(pc->flags & CF_OUTDOOR)) white = 0;
        render_to_rgb(s_rgb, s_state == ST_PLAY ? G.pl.hurt : 0, s_state == ST_PLAY ? G.pl.bonus : 0, white);
        plat_present(s_rgb, SCR_W, SCR_H);
        audio_update();

        if (getenv("NOCTIS_FPS")) {
            static int fps_frames;
            static double acc_t, work_t;
            fps_frames++;
            acc_t += dt;
            work_t += plat_time() - now;
            if (acc_t >= 1.0) {
                fprintf(stderr, "fps %.1f, frame work %.2f ms\n", fps_frames / acc_t, work_t / fps_frames * 1000);
                fps_frames = 0;
                acc_t = work_t = 0;
            }
        }
        double spent = plat_time() - now;
        if (spent < 1.0 / 62) plat_sleep(1.0 / 62 - spent);
    }
    audio_shutdown();
    plat_shutdown();
    return 0;
}
