/*
 * Platform layer on SDL3: window, input, and presenting the software
 * framebuffer through a streaming texture (letterboxed, nearest-neighbour).
 * SDL picks the video backend at runtime (Wayland, X11, KMSDRM, ...).
 */
#include "common.h"

#include <SDL3/SDL.h>

Input g_in;

static SDL_Window *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture *s_tex;
static bool s_captured;

static int key_from_scancode(SDL_Scancode sc)
{
    /* scancodes, so WASD stays under the same fingers on any layout */
    switch (sc) {
    case SDL_SCANCODE_W: return K_W;
    case SDL_SCANCODE_A: return K_A;
    case SDL_SCANCODE_S: return K_S;
    case SDL_SCANCODE_D: return K_D;
    case SDL_SCANCODE_E: return K_E;
    case SDL_SCANCODE_Q: return K_Q;
    case SDL_SCANCODE_F: return K_F;
    case SDL_SCANCODE_M: return K_M;
    case SDL_SCANCODE_1: return K_1;
    case SDL_SCANCODE_2: return K_2;
    case SDL_SCANCODE_3: return K_3;
    case SDL_SCANCODE_SPACE: return K_SPACE;
    case SDL_SCANCODE_EQUALS: case SDL_SCANCODE_KP_PLUS: return K_PLUS;
    case SDL_SCANCODE_MINUS: case SDL_SCANCODE_KP_MINUS: return K_MINUS;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return K_ENTER;
    case SDL_SCANCODE_ESCAPE: return K_ESC;
    case SDL_SCANCODE_TAB: return K_TAB;
    case SDL_SCANCODE_LEFT: return K_LEFT;
    case SDL_SCANCODE_UP: return K_UP;
    case SDL_SCANCODE_RIGHT: return K_RIGHT;
    case SDL_SCANCODE_DOWN: return K_DOWN;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return K_SHIFT;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return K_CTRL;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return K_ALT;
    case SDL_SCANCODE_F11: return K_F11;
    default: return K_NONE;
    }
}

static int button_index(Uint8 b)
{
    switch (b) {
    case SDL_BUTTON_LEFT: return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    case SDL_BUTTON_RIGHT: return 3;
    default: return 0;
    }
}

static void release_all(void)
{
    memset(g_in.down, 0, sizeof g_in.down);
    memset(g_in.mouse, 0, sizeof g_in.mouse);
}

bool plat_init(const char *title, int w, int h)
{
    SDL_SetAppMetadata(title, "1.0.0", "noctis");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "sdl: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(title, w, h, SDL_WINDOW_RESIZABLE, &s_win, &s_ren)) {
        fprintf(stderr, "sdl: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(s_ren, 1);
    SDL_SetRenderLogicalPresentation(s_ren, SCR_W, SCR_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    if (!s_tex) {
        fprintf(stderr, "sdl: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(s_tex, SDL_SCALEMODE_NEAREST);
    g_in.focused = true;
    return true;
}

void plat_shutdown(void)
{
    if (s_tex) SDL_DestroyTexture(s_tex);
    if (s_ren) SDL_DestroyRenderer(s_ren);
    if (s_win) SDL_DestroyWindow(s_win);
    s_tex = NULL;
    s_ren = NULL;
    s_win = NULL;
    SDL_Quit();
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */
void plat_capture_mouse(bool on)
{
    if (!s_win || on == s_captured) return;
    SDL_SetWindowRelativeMouseMode(s_win, on);
    s_captured = on;
}

bool plat_mouse_captured(void) { return s_captured; }

void plat_toggle_fullscreen(void)
{
    if (!s_win) return;
    SDL_SetWindowFullscreen(s_win, !(SDL_GetWindowFlags(s_win) & SDL_WINDOW_FULLSCREEN));
}

void plat_poll(void)
{
    memset(g_in.pressed, 0, sizeof g_in.pressed);
    memset(g_in.mouse_pressed, 0, sizeof g_in.mouse_pressed);
    g_in.wheel = 0;
    g_in.mdx = g_in.mdy = 0;
    if (!s_win) return;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            g_in.quit = true;
            break;
        case SDL_EVENT_KEY_DOWN: {
            int k = key_from_scancode(e.key.scancode);
            if (k && !e.key.repeat) {
                if (!g_in.down[k]) g_in.pressed[k] = true;
                g_in.down[k] = true;
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            int k = key_from_scancode(e.key.scancode);
            if (k) g_in.down[k] = false;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            int b = button_index(e.button.button);
            if (b) { g_in.mouse[b] = true; g_in.mouse_pressed[b] = true; }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int b = button_index(e.button.button);
            if (b) g_in.mouse[b] = false;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            float y = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
            if (y > 0) g_in.wheel++;
            else if (y < 0) g_in.wheel--;
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            if (s_captured) { g_in.mdx += e.motion.xrel; g_in.mdy += e.motion.yrel; }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            g_in.focused = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_in.focused = false;
            release_all();
            if (s_captured) plat_capture_mouse(false);
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* presentation                                                        */
/* ------------------------------------------------------------------ */
void plat_present(const u32 *rgb, int w, int h)
{
    if (!s_ren) return;
    SDL_Rect r = {0, 0, w, h};
    SDL_UpdateTexture(s_tex, &r, rgb, w * 4);
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
    SDL_RenderClear(s_ren);
    SDL_RenderTexture(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
}

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */
double plat_time(void) { return (double)SDL_GetTicksNS() * 1e-9; }

void plat_sleep(double s)
{
    if (s <= 0) return;
    SDL_DelayPrecise((Uint64)(s * 1e9));
}
