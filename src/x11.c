/*
 * Minimal X11 client speaking the wire protocol directly over a socket.
 * No Xlib / XCB: we build requests by hand, parse replies and events,
 * and present frames through MIT-SHM (falling back to PutImage strips).
 */
#include "common.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

Input g_in;

static int s_fd = -1;
static bool s_local;
static u32 s_id_base, s_id_inc, s_id_next;
static u32 s_root, s_win, s_gc, s_blank_cursor;
static u8 s_depth;
static u16 s_max_req;
static u8 s_min_kc, s_max_kc;
static u8 s_keymap[256];
static u16 s_seq;
static u32 s_atom_protocols, s_atom_delete, s_atom_state, s_atom_fullscreen;
static int s_win_w, s_win_h;
static bool s_captured, s_warp_pending;
static int s_last_mx, s_last_my;
static u16 s_last_error_seq;
static u8 s_last_error_code;

static bool s_shm;
static u8 s_shm_opcode, s_shm_event;
static u32 s_shmseg;
static int s_shmid = -1;
static u8 *s_shmaddr;
static size_t s_shmsize;
static bool s_shm_busy;

static u32 *s_frame;
static size_t s_frame_cap;
static int s_frame_w, s_frame_h;

static u8 s_out[1 << 16];
static int s_outlen;
static u8 s_in[1 << 18];
static int s_inlen;
static u8 s_reply[1 << 18];

/* ------------------------------------------------------------------ */
/* byte helpers                                                        */
/* ------------------------------------------------------------------ */
static inline u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static inline u32 rd32(const u8 *p) { return (u32)(p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24); }
static inline void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static inline int pad4(int n) { return (n + 3) & ~3; }

static bool write_all(const void *buf, size_t n)
{
    const u8 *p = buf;
    while (n > 0) {
        ssize_t w = write(s_fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            g_in.quit = true;
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}

static void xflush(void)
{
    if (s_outlen > 0) write_all(s_out, (size_t)s_outlen);
    s_outlen = 0;
}

/* queue a request; returns its sequence number */
static u16 xreq(const void *data, int n)
{
    if (s_outlen + n > (int)sizeof s_out) xflush();
    if (n > (int)sizeof s_out) write_all(data, (size_t)n);
    else { memcpy(s_out + s_outlen, data, (size_t)n); s_outlen += n; }
    return ++s_seq;
}

static u32 new_id(void)
{
    s_id_next += s_id_inc;
    return s_id_base | s_id_next;
}

/* ------------------------------------------------------------------ */
/* reading                                                             */
/* ------------------------------------------------------------------ */
static int read_some(int timeout_ms)
{
    struct pollfd p = {s_fd, POLLIN, 0};
    int r = poll(&p, 1, timeout_ms);
    if (r <= 0) return 0;
    if (s_inlen >= (int)sizeof s_in) return 0;
    ssize_t n = read(s_fd, s_in + s_inlen, sizeof s_in - (size_t)s_inlen);
    if (n <= 0) {
        if (n < 0 && errno == EINTR) return 0;
        g_in.quit = true;
        return -1;
    }
    s_inlen += (int)n;
    return (int)n;
}

static int packet_len(int off)
{
    if (s_inlen - off < 32) return 0;
    const u8 *p = s_in + off;
    int len = 32;
    if (p[0] == 1 || (p[0] & 0x7f) == 35) len += 4 * (int)rd32(p + 4);
    return s_inlen - off >= len ? len : 0;
}

static void consume(int len)
{
    memmove(s_in, s_in + len, (size_t)(s_inlen - len));
    s_inlen -= len;
}

static int key_from_sym(u32 s)
{
    if (s >= 'A' && s <= 'Z') s += 32;
    switch (s) {
    case 'w': return K_W;
    case 'a': return K_A;
    case 's': return K_S;
    case 'd': return K_D;
    case 'e': return K_E;
    case 'q': return K_Q;
    case 'f': return K_F;
    case 'm': return K_M;
    case '1': return K_1;
    case '2': return K_2;
    case '3': return K_3;
    case ' ': return K_SPACE;
    case '+': case '=': case 0xffab: return K_PLUS;
    case '-': case 0xffad: return K_MINUS;
    case 0xff0d: case 0xff8d: return K_ENTER;
    case 0xff1b: return K_ESC;
    case 0xff09: return K_TAB;
    case 0xff51: return K_LEFT;
    case 0xff52: return K_UP;
    case 0xff53: return K_RIGHT;
    case 0xff54: return K_DOWN;
    case 0xffe1: case 0xffe2: return K_SHIFT;
    case 0xffe3: case 0xffe4: return K_CTRL;
    case 0xffe9: case 0xffea: return K_ALT;
    case 0xffc8: return K_F11;
    default: return K_NONE;
    }
}

static void release_all(void)
{
    memset(g_in.down, 0, sizeof g_in.down);
    memset(g_in.mouse, 0, sizeof g_in.mouse);
}

static void handle_packet(const u8 *p, int len, const u8 *next)
{
    (void)len;
    u8 type = p[0] & 0x7f;
    switch (type) {
    case 0:
        s_last_error_seq = rd16(p + 2);
        s_last_error_code = p[1];
        if (!(s_shm && p[10] == s_shm_opcode))
            fprintf(stderr, "x11: error %d (request %d.%d, seq %d)\n", p[1], p[10], rd16(p + 8), rd16(p + 2));
        break;
    case 1:
        break; /* reply nobody waited for (e.g. GrabPointer) */
    case 2: { /* KeyPress */
        int k = s_keymap[p[1]];
        if (k) {
            if (!g_in.down[k]) g_in.pressed[k] = true;
            g_in.down[k] = true;
        }
        break;
    }
    case 3: { /* KeyRelease: swallow auto-repeat (release followed by press with same time) */
        if (next && (next[0] & 0x7f) == 2 && next[1] == p[1] && rd32(next + 4) == rd32(p + 4)) break;
        int k = s_keymap[p[1]];
        if (k) g_in.down[k] = false;
        break;
    }
    case 4: { /* ButtonPress */
        int b = p[1];
        if (b >= 1 && b <= 3) { g_in.mouse[b] = true; g_in.mouse_pressed[b] = true; }
        else if (b == 4) g_in.wheel++;
        else if (b == 5) g_in.wheel--;
        break;
    }
    case 5: {
        int b = p[1];
        if (b >= 1 && b <= 3) g_in.mouse[b] = false;
        break;
    }
    case 6: { /* MotionNotify */
        int x = (i16)rd16(p + 24), y = (i16)rd16(p + 26);
        if (s_captured) {
            if (s_warp_pending && x == s_win_w / 2 && y == s_win_h / 2) s_warp_pending = false;
            else { g_in.mdx += (float)(x - s_last_mx); g_in.mdy += (float)(y - s_last_my); }
        }
        s_last_mx = x;
        s_last_my = y;
        break;
    }
    case 9:
        if (p[8] == 1 || p[8] == 2) break; /* NotifyGrab / NotifyUngrab */
        g_in.focused = true;
        break;
    case 10:
        if (p[8] == 1 || p[8] == 2 || p[1] == 5) break; /* grab changes, NotifyPointer */
        g_in.focused = false;
        release_all();
        if (s_captured) plat_capture_mouse(false);
        break;
    case 22: { /* ConfigureNotify */
        int w = rd16(p + 20), h = rd16(p + 22);
        if (w > 0 && h > 0) { s_win_w = w; s_win_h = h; }
        break;
    }
    case 33: /* ClientMessage */
        if (rd32(p + 8) == s_atom_protocols && rd32(p + 12) == s_atom_delete) g_in.quit = true;
        break;
    default:
        if (s_shm && type == s_shm_event) s_shm_busy = false;
        break;
    }
}

static void process_buffered(void)
{
    int off = 0, len;
    while ((len = packet_len(off)) > 0) {
        int nlen = packet_len(off + len);
        handle_packet(s_in + off, len, nlen ? s_in + off + len : NULL);
        off += len;
    }
    if (off) consume(off);
}

static u8 *wait_reply(u16 seq)
{
    xflush();
    for (int spins = 0; spins < 5000;) {
        int len = packet_len(0);
        if (!len) {
            int r = read_some(10);
            if (r < 0) return NULL;
            if (r == 0) spins++;
            continue;
        }
        u8 type = s_in[0];
        u16 pseq = rd16(s_in + 2);
        if (type == 1 && pseq == seq) {
            memcpy(s_reply, s_in, (size_t)len);
            consume(len);
            return s_reply;
        }
        handle_packet(s_in, len, NULL);
        consume(len);
        if (type == 0 && pseq == seq) return NULL;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* connection                                                          */
/* ------------------------------------------------------------------ */
static bool read_auth(const char *dnum, u8 *name, int *nlen, u8 *data, int *dlen)
{
    const char *path = getenv("XAUTHORITY");
    char buf[512];
    if (!path || !*path) {
        const char *home = getenv("HOME");
        if (!home) return false;
        snprintf(buf, sizeof buf, "%s/.Xauthority", home);
        path = buf;
    }
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool found = false;
    for (;;) {
        u8 hdr[2];
        u8 fields[4][256];
        int flen[4];
        if (fread(hdr, 1, 2, f) != 2) break;
        bool ok = true;
        for (int i = 0; i < 4 && ok; i++) {
            u8 l[2];
            if (fread(l, 1, 2, f) != 2) { ok = false; break; }
            flen[i] = l[0] << 8 | l[1];
            if (flen[i] > 255 || fread(fields[i], 1, (size_t)flen[i], f) != (size_t)flen[i]) ok = false;
        }
        if (!ok) break;
        bool num_ok = flen[1] == 0 || (flen[1] == (int)strlen(dnum) && !memcmp(fields[1], dnum, (size_t)flen[1]));
        if (num_ok && flen[2] == 18 && !memcmp(fields[2], "MIT-MAGIC-COOKIE-1", 18)) {
            memcpy(name, fields[2], 18);
            *nlen = 18;
            memcpy(data, fields[3], (size_t)flen[3]);
            *dlen = flen[3];
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

static bool connect_display(char *dnum_out)
{
    const char *disp = getenv("DISPLAY");
    if (!disp || !*disp) disp = ":0";
    const char *colon = strrchr(disp, ':');
    if (!colon) { fprintf(stderr, "x11: bad DISPLAY '%s'\n", disp); return false; }
    char host[256] = {0};
    size_t hl = (size_t)(colon - disp);
    if (hl >= sizeof host) hl = sizeof host - 1;
    memcpy(host, disp, hl);
    int num = atoi(colon + 1);
    snprintf(dnum_out, 16, "%d", num);

    if (!host[0] || !strcmp(host, "unix")) {
        s_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s_fd < 0) return false;
        struct sockaddr_un sa;
        memset(&sa, 0, sizeof sa);
        sa.sun_family = AF_UNIX;
        /* abstract socket first, then the filesystem path */
        int n = snprintf(sa.sun_path + 1, sizeof sa.sun_path - 1, "/tmp/.X11-unix/X%d", num);
        if (connect(s_fd, (struct sockaddr *)&sa, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n)) == 0) {
            s_local = true;
            return true;
        }
        memset(&sa, 0, sizeof sa);
        sa.sun_family = AF_UNIX;
        snprintf(sa.sun_path, sizeof sa.sun_path, "/tmp/.X11-unix/X%d", num);
        if (connect(s_fd, (struct sockaddr *)&sa, sizeof sa) == 0) {
            s_local = true;
            return true;
        }
        close(s_fd);
        s_fd = -1;
        fprintf(stderr, "x11: cannot connect to %s\n", disp);
        return false;
    }
    char port[16];
    snprintf(port, sizeof port, "%d", 6000 + num);
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return false;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        s_fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s_fd < 0) continue;
        if (connect(s_fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close(s_fd);
        s_fd = -1;
    }
    freeaddrinfo(res);
    return s_fd >= 0;
}

static u32 intern_atom(const char *name)
{
    int n = (int)strlen(name);
    u8 req[256] = {0};
    req[0] = 16;
    wr16(req + 2, (u16)(2 + pad4(n) / 4));
    wr16(req + 4, (u16)n);
    memcpy(req + 8, name, (size_t)n);
    u8 *r = wait_reply(xreq(req, 8 + pad4(n)));
    return r ? rd32(r + 8) : 0;
}

static void change_property(u32 prop, u32 type, int format, const void *data, int nelem)
{
    int bytes = nelem * (format / 8);
    u8 req[512] = {0};
    if (bytes > 480) return;
    req[0] = 18;
    req[1] = 0;
    wr16(req + 2, (u16)(6 + pad4(bytes) / 4));
    wr32(req + 4, s_win);
    wr32(req + 8, prop);
    wr32(req + 12, type);
    req[16] = (u8)format;
    wr32(req + 20, (u32)nelem);
    memcpy(req + 24, data, (size_t)bytes);
    xreq(req, 24 + pad4(bytes));
}

static void load_keymap(void)
{
    int count = s_max_kc - s_min_kc + 1;
    u8 req[8] = {101, 0, 2, 0, s_min_kc, (u8)count, 0, 0};
    u8 *r = wait_reply(xreq(req, 8));
    if (!r) return;
    int per = r[1];
    memset(s_keymap, 0, sizeof s_keymap);
    for (int i = 0; i < count; i++) {
        const u8 *ks = r + 32 + i * per * 4;
        u32 sym = per > 0 ? rd32(ks) : 0;
        if (!sym && per > 1) sym = rd32(ks + 4);
        s_keymap[s_min_kc + i] = (u8)key_from_sym(sym);
    }
}

static void shm_init(void)
{
    if (!s_local || getenv("NOCTIS_NO_SHM")) return;
    const char *name = "MIT-SHM";
    u8 req[20] = {0};
    req[0] = 98;
    wr16(req + 2, (u16)(2 + pad4(7) / 4));
    wr16(req + 4, 7);
    memcpy(req + 8, name, 7);
    u8 *r = wait_reply(xreq(req, 8 + pad4(7)));
    if (!r || !r[8]) return;
    s_shm_opcode = r[9];
    s_shm_event = r[10];
    s_shm = true;
}

static void shm_free(void)
{
    if (!s_shmaddr) return;
    u8 req[8] = {s_shm_opcode, 2, 2, 0};
    wr32(req + 4, s_shmseg);
    xreq(req, 8);
    xflush();
    shmdt(s_shmaddr);
    s_shmaddr = NULL;
    s_shmsize = 0;
}

static bool shm_alloc(size_t size)
{
    shm_free();
    s_shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
    if (s_shmid < 0) return false;
    s_shmaddr = shmat(s_shmid, NULL, 0);
    if (s_shmaddr == (void *)-1) {
        s_shmaddr = NULL;
        shmctl(s_shmid, IPC_RMID, NULL);
        return false;
    }
    s_shmseg = new_id();
    u8 req[16] = {s_shm_opcode, 1, 4, 0};
    wr32(req + 4, s_shmseg);
    wr32(req + 8, (u32)s_shmid);
    req[12] = 0;
    u16 seq = xreq(req, 16);
    u8 sync[4] = {43, 0, 1, 0}; /* GetInputFocus round trip */
    s_last_error_seq = 0;
    wait_reply(xreq(sync, 4));
    shmctl(s_shmid, IPC_RMID, NULL);
    if (s_last_error_seq == seq) {
        shmdt(s_shmaddr);
        s_shmaddr = NULL;
        return false;
    }
    s_shmsize = size;
    return true;
}

bool plat_init(const char *title, int w, int h)
{
    char dnum[16];
    if (!connect_display(dnum)) return false;

    u8 aname[32], adata[256];
    int nlen = 0, dlen = 0;
    read_auth(dnum, aname, &nlen, adata, &dlen);
    u8 setup[12 + 32 + 256] = {0};
    setup[0] = 'l';
    wr16(setup + 2, 11);
    wr16(setup + 4, 0);
    wr16(setup + 6, (u16)nlen);
    wr16(setup + 8, (u16)dlen);
    memcpy(setup + 12, aname, (size_t)nlen);
    memcpy(setup + 12 + pad4(nlen), adata, (size_t)dlen);
    if (!write_all(setup, (size_t)(12 + pad4(nlen) + pad4(dlen)))) return false;

    while (s_inlen < 8) if (read_some(5000) <= 0) { fprintf(stderr, "x11: no setup reply\n"); return false; }
    int total = 8 + 4 * rd16(s_in + 6);
    while (s_inlen < total) if (read_some(5000) <= 0) return false;
    if (s_in[0] != 1) {
        fprintf(stderr, "x11: connection refused: %.*s\n", s_in[1], s_in + 8);
        return false;
    }
    const u8 *d = s_in + 8;
    s_id_base = rd32(d + 4);
    u32 mask = rd32(d + 8);
    s_id_inc = mask & (~mask + 1);
    s_id_next = 0;
    int vlen = rd16(d + 16);
    s_max_req = rd16(d + 18);
    int nformats = d[21];
    if (d[22] != 0) { fprintf(stderr, "x11: big-endian image order unsupported\n"); return false; }
    s_min_kc = d[26];
    s_max_kc = d[27];
    const u8 *fmt = d + 32 + pad4(vlen);
    const u8 *scr = fmt + nformats * 8;
    s_root = rd32(scr);
    s_depth = scr[38];
    int bpp = 0;
    for (int i = 0; i < nformats; i++)
        if (fmt[i * 8] == s_depth) bpp = fmt[i * 8 + 1];
    if (s_depth < 24 || bpp != 32) {
        fprintf(stderr, "x11: need a 24/32-bit TrueColor screen (depth %d, bpp %d)\n", s_depth, bpp);
        return false;
    }
    consume(total);

    s_win_w = w;
    s_win_h = h;
    s_win = new_id();
    {
        u8 req[40] = {0};
        req[0] = 1;
        req[1] = 0;
        wr16(req + 2, 10);
        wr32(req + 4, s_win);
        wr32(req + 8, s_root);
        wr16(req + 16, (u16)w);
        wr16(req + 18, (u16)h);
        wr16(req + 22, 1);       /* InputOutput */
        wr32(req + 24, 0);       /* CopyFromParent visual */
        wr32(req + 28, 0x2 | 0x800);
        wr32(req + 32, 0);       /* background black */
        wr32(req + 36, 0x1 | 0x2 | 0x4 | 0x8 | 0x40 | 0x8000 | 0x20000 | 0x200000);
        xreq(req, 40);
    }
    s_atom_protocols = intern_atom("WM_PROTOCOLS");
    s_atom_delete = intern_atom("WM_DELETE_WINDOW");
    s_atom_state = intern_atom("_NET_WM_STATE");
    s_atom_fullscreen = intern_atom("_NET_WM_STATE_FULLSCREEN");
    u32 net_name = intern_atom("_NET_WM_NAME"), utf8 = intern_atom("UTF8_STRING");
    change_property(39, 31, 8, title, (int)strlen(title));
    if (net_name && utf8) change_property(net_name, utf8, 8, title, (int)strlen(title));
    change_property(67, 31, 8, "noctis\0Noctis", 14);
    if (s_atom_protocols && s_atom_delete) change_property(s_atom_protocols, 4, 32, &s_atom_delete, 1);

    s_gc = new_id();
    {
        u8 req[20] = {55, 0, 5, 0};
        wr32(req + 4, s_gc);
        wr32(req + 8, s_win);
        wr32(req + 12, 0x10000);
        wr32(req + 16, 0);
        xreq(req, 20);
    }
    /* invisible cursor from a cleared 1x1 bitmap */
    {
        u32 pm = new_id(), pgc = new_id();
        s_blank_cursor = new_id();
        u8 cp[16] = {53, 1, 4, 0};
        wr32(cp + 4, pm);
        wr32(cp + 8, s_win);
        wr16(cp + 12, 1);
        wr16(cp + 14, 1);
        xreq(cp, 16);
        u8 gc[20] = {55, 0, 5, 0};
        wr32(gc + 4, pgc);
        wr32(gc + 8, pm);
        wr32(gc + 12, 0x4);
        wr32(gc + 16, 0);
        xreq(gc, 20);
        u8 fr[20] = {70, 0, 5, 0};
        wr32(fr + 4, pm);
        wr32(fr + 8, pgc);
        wr16(fr + 16, 1);
        wr16(fr + 18, 1);
        xreq(fr, 20);
        u8 cc[32] = {93, 0, 8, 0};
        wr32(cc + 4, s_blank_cursor);
        wr32(cc + 8, pm);
        wr32(cc + 12, pm);
        xreq(cc, 32);
    }
    load_keymap();
    shm_init();
    {
        u8 req[8] = {8, 0, 2, 0};
        wr32(req + 4, s_win);
        xreq(req, 8);
    }
    xflush();
    g_in.focused = true;
    return true;
}

void plat_shutdown(void)
{
    if (s_fd < 0) return;
    if (s_captured) plat_capture_mouse(false);
    shm_free();
    xflush();
    close(s_fd);
    s_fd = -1;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */
static void warp_center(void)
{
    u8 req[24] = {41, 0, 6, 0};
    wr32(req + 4, 0);
    wr32(req + 8, s_win);
    wr16(req + 20, (u16)(s_win_w / 2));
    wr16(req + 22, (u16)(s_win_h / 2));
    xreq(req, 24);
    s_warp_pending = true;
}

void plat_capture_mouse(bool on)
{
    if (s_fd < 0 || on == s_captured) return;
    u8 cw[16] = {2, 0, 4, 0};
    wr32(cw + 4, s_win);
    wr32(cw + 8, 0x4000);
    wr32(cw + 12, on ? s_blank_cursor : 0);
    xreq(cw, 16);
    if (on) {
        u8 g[24] = {26, 1, 6, 0};
        wr32(g + 4, s_win);
        wr16(g + 8, 0x4 | 0x8 | 0x40);
        g[10] = 1;
        g[11] = 1;
        wr32(g + 12, s_win);
        wr32(g + 16, 0);
        wr32(g + 20, 0);
        xreq(g, 24);
        warp_center();
    } else {
        u8 u[8] = {27, 0, 2, 0};
        xreq(u, 8);
    }
    s_captured = on;
    xflush();
}

bool plat_mouse_captured(void) { return s_captured; }

void plat_toggle_fullscreen(void)
{
    if (!s_atom_state || !s_atom_fullscreen) return;
    u8 req[44] = {25, 0, 11, 0};
    wr32(req + 4, s_root);
    wr32(req + 8, 0x180000);
    u8 *ev = req + 12;
    ev[0] = 33;
    ev[1] = 32;
    wr32(ev + 4, s_win);
    wr32(ev + 8, s_atom_state);
    wr32(ev + 12, 2);
    wr32(ev + 16, s_atom_fullscreen);
    wr32(ev + 24, 1);
    xreq(req, 44);
    xflush();
}

void plat_poll(void)
{
    memset(g_in.pressed, 0, sizeof g_in.pressed);
    memset(g_in.mouse_pressed, 0, sizeof g_in.mouse_pressed);
    g_in.wheel = 0;
    g_in.mdx = g_in.mdy = 0;
    if (s_fd < 0) return;
    while (read_some(0) > 0) {}
    process_buffered();
    if (s_captured && !s_warp_pending &&
        (abs(s_last_mx - s_win_w / 2) > s_win_w / 4 || abs(s_last_my - s_win_h / 2) > s_win_h / 4))
        warp_center();
    xflush();
}

/* ------------------------------------------------------------------ */
/* presentation                                                        */
/* ------------------------------------------------------------------ */
void plat_present(const u32 *rgb, int w, int h)
{
    if (s_fd < 0) return;
    int ww = s_win_w, wh = s_win_h;
    size_t need = (size_t)ww * wh * 4;
    bool resized = ww != s_frame_w || wh != s_frame_h;

    if (s_shm && s_shm_busy) {
        for (int i = 0; i < 200 && s_shm_busy && !g_in.quit; i++) {
            if (read_some(2) < 0) return;
            process_buffered();
        }
        s_shm_busy = false;
    }
    if (s_shm && (resized || !s_shmaddr)) {
        if (!shm_alloc(need)) {
            fprintf(stderr, "x11: MIT-SHM unavailable, using PutImage\n");
            s_shm = false;
        }
    }
    u32 *dst;
    if (s_shm) dst = (u32 *)s_shmaddr;
    else {
        if (s_frame_cap < need) { free(s_frame); s_frame = malloc(need); s_frame_cap = need; }
        dst = s_frame;
    }
    if (!dst) return;
    if (resized) memset(dst, 0, need);
    s_frame_w = ww;
    s_frame_h = wh;

    float scale = minf((float)ww / w, (float)wh / h);
    int dw = (int)(w * scale), dh = (int)(h * scale);
    if (dw < 1 || dh < 1) return;
    int ox = (ww - dw) / 2, oy = (wh - dh) / 2;
    static int *xmap;
    static int xmap_n;
    if (xmap_n < dw) { free(xmap); xmap = malloc(sizeof(int) * (size_t)dw); xmap_n = dw; }
    for (int x = 0; x < dw; x++) xmap[x] = x * w / dw;
    int prev_sy = -1;
    for (int y = 0; y < dh; y++) {
        int sy = y * h / dh;
        u32 *row = dst + (size_t)(oy + y) * ww + ox;
        if (sy == prev_sy) { memcpy(row, row - ww, (size_t)dw * 4); continue; }
        const u32 *src = rgb + (size_t)sy * w;
        for (int x = 0; x < dw; x++) row[x] = src[xmap[x]];
        prev_sy = sy;
    }

    if (s_shm) {
        u8 req[40] = {s_shm_opcode, 3, 10, 0};
        wr32(req + 4, s_win);
        wr32(req + 8, s_gc);
        wr16(req + 12, (u16)ww);
        wr16(req + 14, (u16)wh);
        wr16(req + 20, (u16)ww);
        wr16(req + 22, (u16)wh);
        req[28] = s_depth;
        req[29] = 2;
        req[30] = 1;
        wr32(req + 32, s_shmseg);
        wr32(req + 36, 0);
        xreq(req, 40);
        s_shm_busy = true;
    } else {
        int max_bytes = (int)s_max_req * 4 - 24;
        int rows = max_bytes / (ww * 4);
        if (rows < 1) rows = 1;
        for (int y = 0; y < wh; y += rows) {
            int n = y + rows > wh ? wh - y : rows;
            u8 hdr[24] = {72, 2};
            wr16(hdr + 2, (u16)(6 + n * ww));
            wr32(hdr + 4, s_win);
            wr32(hdr + 8, s_gc);
            wr16(hdr + 12, (u16)ww);
            wr16(hdr + 14, (u16)n);
            wr16(hdr + 16, 0);
            wr16(hdr + 18, (u16)y);
            hdr[21] = s_depth;
            xreq(hdr, 24);
            xflush();
            write_all(dst + (size_t)y * ww, (size_t)n * ww * 4);
        }
    }
    xflush();
}

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */
double plat_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void plat_sleep(double s)
{
    if (s <= 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}
