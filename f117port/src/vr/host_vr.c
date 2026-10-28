/* VR: host.h on Android (Meta Quest) without SDL. Replaces host.c in libf117.so (f117port/android).
 * The engine runs f117_main (main.c built with -Dmain=f117_main) on its own thread; the StereoKit app
 * polls the picture and the sound and feeds input through f117vr.h. Input arrives on the app's threads and
 * is queued; host_pump hands it to the engine's handlers on the engine thread, as host.c does with SDL events. */
#include "host.h"
#include "keymap.h"
#include "vr/f117vr.h"
#include "prog.h"
#include "vr/vr_world.h"
#include "vr/vr_fdm.h"

#include <dirent.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "opl3.h"
#include "enhanced/enh_canvas.h"
#include "enhanced/enhanced.h"
#include "enhanced/sandbox.h"

int f117_main(int argc, char **argv);            /* main.c */

static char *game_dir;
static bool aspect43 = true;
static volatile bool quit_requested;

static void (*tick_handler)(void);
static void (*kbd_handler)(u8);
static void (*focus_lost_handler)(void);
static void (*mouse_handler)(int, int, u8);
static bool (*frame_source)(u32 *);
static u32 *frame;
static size_t frame_cap;
static int frame_w = 320, frame_h = 200;
static int nominal_w = 1280, nominal_h = 960;     /* f117vr_set_view_size */

static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;     /* input queue, joystick, mouse, view size */
static pthread_mutex_t pic_lock = PTHREAD_MUTEX_INITIALIZER;    /* the shared picture */
/* Recursive, as SDL_Mutex is: the sound code takes the lock around its work and host_opl_write takes it again. */
static pthread_mutex_t audio_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static int view_w(void) { return frame_w; }
static int view_h(void)
{
    if (enh_canvas_active()) return aspect43 ? frame_h * 6 / 5 : frame_h;
    return aspect43 ? frame_w * 3 / 4 : frame_h;
}

static void frame_reserve(int w, int h)
{
    size_t n = (size_t)w * (size_t)h;
    if (n <= frame_cap) return;
    free(frame);
    frame = calloc(n, sizeof(u32));
    if (!frame) host_fatal("out of memory for a %d x %d frame", w, h);
    frame_cap = n;
}

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t clock_start_ns;
static u64 ticks_run;
static uint64_t clk_ns(void) { return mono_ns(); }

static opl3_chip opl;
static u8 opl_status;

/* The sound's timing. The game's sound driver runs in the 70 Hz tick, and the ticks run in host_pump: while the engine draws a
 * frame (tens of ms on the Quest) none run, then the missed ones run back to back. Written straight to the chip, the notes and
 * envelope steps of those ticks sound at once, the sound stepping with the frame rate. So each register write is stamped with the
 * time of its tick (or the clock, outside a tick), and f117vr_audio plays it at that time, a fixed AUDIO_LAG_NS behind. */
#define AUDIO_LAG_NS 50000000ull
#define OPLQ 8192
static struct { uint64_t ns; u8 reg, value; } oplq[OPLQ];
static int oplq_head, oplq_tail;
static uint64_t in_tick_ns;                      /* the due time of the tick running now, 0 outside one */
static uint64_t audio_base_ns, audio_samples;   /* the next sample f117vr_audio makes is at base + samples / rate (base 0 before the first) */
#define AUDIO_NS() (audio_base_ns + audio_samples * 1000000000ull / F117VR_AUDIO_RATE)

/* ---------------------------------------------------------------- the app side's state */

#define KEYQ 256
static u16 keyq[KEYQ];                           /* xt (bit 8 grey) | bit 15 = released */
static int keyq_head, keyq_tail;
static bool joy_present;
static s16 joy_axes[4];
static u8 joy_buttons;
static float mouse_u, mouse_v;
static u8 mouse_buttons;
static bool mouse_changed;

static bool world_mask;                          /* f117vr_set_world_mask: the 3D window's world pixels transparent */
static u8 *cls;                                  /* the compositor's pixel classes (enh_cls_out) */
static size_t cls_cap;
static u32 *pic;                                 /* the last presented picture, RGBA */
static size_t pic_cap;
static int pic_w, pic_h, pic_serial;
static float pic_aspect = 4.0f / 3.0f;

static volatile int state = F117VR_IDLE;
static char fatal_msg[512];

/* ---------------------------------------------------------------- init */

bool host_init(const HostConfig *cfg)
{
    game_dir = strdup(cfg->game_dir);
    aspect43 = cfg->aspect_4_3;
    frame_reserve(HOST_FRAME_MAX_W, HOST_FRAME_MAX_H);
    clock_start_ns = clk_ns();
    ticks_run = 0;
    OPL3_Reset(&opl, F117VR_AUDIO_RATE);
    oplq_head = oplq_tail = 0;
    audio_base_ns = audio_samples = 0;
    host_set_frame_source(NULL, 320, 200);
    return true;
}

void host_shutdown(void)
{
    free(game_dir);
    game_dir = NULL;
}

void host_set_tick_handler(void (*handler)(void)) { tick_handler = handler; }
u64  host_ticks(void) { return ticks_run; }

static int frame_ticks = HOST_DEFAULT_FRAME_TICKS;
void host_set_frame_ticks(int ticks) { frame_ticks = ticks < 0 ? 0 : ticks > 70 ? 70 : ticks; }
int  host_frame_ticks(void) { return frame_ticks; }
bool host_quit_requested(void) { return quit_requested; }
void host_set_kbd_handler(void (*handler)(u8)) { kbd_handler = handler; }
void host_set_focus_lost_handler(void (*handler)(void)) { focus_lost_handler = handler; }
void host_set_mouse_handler(void (*handler)(int, int, u8)) { mouse_handler = handler; }

static void host_resize_frame(int w, int h)
{
    frame_w = w; frame_h = h;
    frame_reserve(w, h);
}

static void canvas_sync(void)
{
    if (!enh_canvas_active()) return;
    pthread_mutex_lock(&io_lock);
    int ww = nominal_w, wh = nominal_h;
    pthread_mutex_unlock(&io_lock);
    enh_canvas_update(ww, wh, aspect43);
    if (enh_present_hold()) return;
    if (enh_cv.lw != frame_w || enh_cv.lh != frame_h) host_resize_frame(enh_cv.lw, enh_cv.lh);
}

void host_set_frame_source(bool (*compose)(u32 *), int w, int h)
{
    frame_source = compose;
    frame_w = w < 1 ? 1 : w > HOST_FRAME_MAX_W ? HOST_FRAME_MAX_W : w;
    frame_h = h < 1 ? 1 : h > HOST_FRAME_MAX_H ? HOST_FRAME_MAX_H : h;
    host_resize_frame(frame_w, frame_h);
}

uint64_t host_ns(void) { return clk_ns() - clock_start_ns; }

static uint64_t tick_due_ns(u64 n) { return clock_start_ns + n * VGA_FRAME_NS_NUM / VGA_FRAME_NS_DEN; }

bool host_trace(void) { return false; }
void host_snapshot_now(const char *tag) { (void)tag; }

/* The picture goes to the app: XRGB -> RGBA (byte order R G B A in memory), alpha 255. */
static void present(void)
{
    const size_t n = (size_t)frame_w * (size_t)frame_h;
    pthread_mutex_lock(&pic_lock);
    if (n > pic_cap) {
        free(pic);
        pic = malloc(n * sizeof(u32));
        pic_cap = pic ? n : 0;
    }
    if (pic) {
        for (size_t i = 0; i < n; i++) {
            const u32 p = frame[i];
            pic[i] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
        }
        /* The headset draws the world itself: in flight the main 3D window's world pixels become transparent, so the picture is
         * the HUD over the world and the cockpit panel (the TV and map windows below row 107 are 3D renders too and stay). */
        if (world_mask && cls && enh_canvas_active() && strncmp(prog_current_name(), "VGAME", 5) == 0) {
            const int n_ = enh_cv.n, fy = enh_cv.fy;
            for (int y = 0; y < frame_h; y++) {
                int row = (y - fy) / (n_ ? n_ : 1);
                if (y < fy || row >= 107) continue;
                u32 *o = pic + (size_t)y * (size_t)frame_w;
                const u8 *c = cls + (size_t)y * (size_t)frame_w;
                for (int x = 0; x < frame_w; x++) if (c[x] == 2 || c[x] == 4) o[x] &= 0x00FFFFFFu;
            }
        }
        pic_w = frame_w;
        pic_h = frame_h;
        pic_aspect = (float)view_w() / (float)view_h();
        pic_serial++;
    }
    pthread_mutex_unlock(&pic_lock);
}

/* ---------------------------------------------------------------- keyboard */

#define GREY 0x100

static void feed(const u8 *bytes, int n)
{
    if (!kbd_handler) return;
    for (int i = 0; i < n; i++) kbd_handler(bytes[i]);
}

/* host.c key_event after xt_scan: the key map, then the make or break bytes. */
static void key_event(u16 x, bool down)
{
    u16 mapped[16];
    int m = keymap_map(x, down, mapped);
    if (m >= 0) {
        for (int i = 0; i < m; i++) {
            u8 seq[2];
            int n = 0;
            if (mapped[i] & GREY) seq[n++] = 0xE0;
            seq[n++] = (u8)(mapped[i] & 0xFF);
            feed(seq, n);
        }
        return;
    }
    u8 seq[2];
    int n = 0;
    if (x & GREY) seq[n++] = 0xE0;
    seq[n++] = (u8)(down ? (x & 0x7F) : (x & 0x7F) | 0x80);
    feed(seq, n);
}

static void process_events(void)
{
    u16 keys[KEYQ];
    int nk = 0;
    bool mc;
    pthread_mutex_lock(&io_lock);
    while (keyq_tail != keyq_head) {
        keys[nk++] = keyq[keyq_tail];
        keyq_tail = (keyq_tail + 1) % KEYQ;
    }
    mc = mouse_changed;
    mouse_changed = false;
    pthread_mutex_unlock(&io_lock);
    for (int i = 0; i < nk; i++) key_event(keys[i] & 0x1FF, !(keys[i] & 0x8000));
    if (mc && mouse_handler) {
        int x, y;
        u8 b;
        host_mouse_read(&x, &y, &b);
        mouse_handler(x, y, b);
    }
}

/* ---------------------------------------------------------------- scripted input (F117_KEYS, a test aid) */

/* host.c's F117_KEYS with a pointer: "<seconds>:<item>,..." from the engine's start. An item is XT codes as in host.c
 * ("1c", "e048", "2a+15"; "p" after the codes only presses, "r" only releases) or the pointer "m<u>/<v>/<buttons>" (u, v in 0..1).
 * The app sets the variable from its launch intent (adb shell am start ... --es keys "..."). */
static void scripted_keys(void)
{
    static const char *spec;
    static bool checked;
    if (!checked) { spec = getenv("F117_KEYS"); checked = true; }
    if (!spec || !*spec || !kbd_handler) return;
    char *end;
    double at = strtod(spec, &end);
    if (end == spec || *end != ':') { spec = NULL; return; }
    if ((double)(clk_ns() - clock_start_ns) / 1e9 < at) return;
    const char *p = end + 1;
    if (*p == 'm') {
        float u = strtof(p + 1, &end), v = 0;
        int b = 0;
        p = end;
        if (*p == '/') { v = strtof(p + 1, &end); p = end; }
        if (*p == '/') { b = (int)strtol(p + 1, &end, 10); p = end; }
        f117vr_mouse(u, v, b);
    } else {
        u16 keys[8];
        int n = 0;
        bool press = true, release = true;
        while (n < 8) {
            keys[n++] = (u16)strtoul(p, &end, 16);
            p = end;
            if (*p == 'p') { release = false; p++; }
            else if (*p == 'r') { press = false; p++; }
            if (*p != '+') break;
            p++;
        }
        if (press)
            for (int i = 0; i < n; i++) {
                if (keys[i] >> 8) kbd_handler((u8)(keys[i] >> 8));
                kbd_handler((u8)keys[i]);
            }
        if (release)
            for (int i = n - 1; i >= 0; i--) {
                if (keys[i] >> 8) kbd_handler((u8)(keys[i] >> 8));
                kbd_handler((u8)(keys[i] | 0x80));
            }
    }
    host_log_error("scripted %.1f s", at);
    spec = *p == ',' ? p + 1 : NULL;
}

/* ---------------------------------------------------------------- pump */

static uint64_t last_present_ns;

void host_pump(void)
{
    if (enh_sandbox_deny("host_pump")) return;
    process_events();
    scripted_keys();
    if (strncmp(prog_current_name(), "VGAME", 5) == 0) {   /* the 3D world for the headset's renderer (vr_world.c) */
        vr_camera_capture();
        vr_world_build();
    }

    bool worked = false;
    uint64_t now = clk_ns();
    int budget = 35;                                /* at most 0.5 s of catch-up per call */
    while (tick_due_ns(ticks_run + 1) <= now && budget-- > 0) {
        ticks_run++;
        in_tick_ns = tick_due_ns(ticks_run);
        if (tick_handler) tick_handler();
        in_tick_ns = 0;
        worked = true;
    }
    if (budget < 0)                                 /* fell too far behind (the headset slept): resynchronise */
        clock_start_ns = now - (tick_due_ns(ticks_run) - clock_start_ns);

    if (frame_source && now - last_present_ns >= 8000000ull) {
        canvas_sync();
        {   /* VR: the pixel classes beside the picture */
            size_t need = (size_t)frame_w * (size_t)frame_h;
            if (need > cls_cap) { free(cls); cls = malloc(need); cls_cap = cls ? need : 0; }
            enh_cls_out = world_mask && cls_cap >= need ? cls : NULL;
        }
        if (frame_source(frame)) {
            present();
            last_present_ns = clk_ns();
            worked = true;
        }
    }
    if (!worked) {
        uint64_t next = tick_due_ns(ticks_run + 1), t = mono_ns();
        if (next > t) {
            uint64_t d = next - t < 1000000ull ? next - t : 1000000ull;
            struct timespec ts = { 0, (long)d };
            nanosleep(&ts, NULL);
        }
    }
}

void host_wait_until_ns(uint64_t t)
{
    if (enh_sbx) return;
    while (host_ns() < t) host_pump();
}

/* ---------------------------------------------------------------- mouse, joystick */

void host_mouse_read(int *x, int *y, u8 *buttons)
{
    pthread_mutex_lock(&io_lock);
    float u = mouse_u, v = mouse_v;
    u8 b = mouse_buttons;
    pthread_mutex_unlock(&io_lock);
    float px = u * (float)view_w(), py = v * (float)view_h();
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px > (float)view_w() - 1) px = (float)view_w() - 1;
    if (py > (float)view_h() - 1) py = (float)view_h() - 1;
    if (x) *x = (int)(px * frame_w / view_w());
    if (y) *y = (int)(py * frame_h / view_h());
    if (x && y && enh_canvas_active()) enh_canvas_to_frame(x, y);
    if (buttons) *buttons = b;
}

void host_mouse_warp(int x, int y)
{
    if (enh_canvas_active()) enh_frame_to_canvas(&x, &y);
    pthread_mutex_lock(&io_lock);
    mouse_u = (float)x / (float)frame_w;
    mouse_v = (float)y / (float)frame_h;
    pthread_mutex_unlock(&io_lock);
}

bool host_joy_present(void) { return joy_present; }

void host_joy_axes(s16 axes[4])
{
    pthread_mutex_lock(&io_lock);
    for (int i = 0; i < 4; i++) axes[i] = joy_present ? joy_axes[i] : 0;
    pthread_mutex_unlock(&io_lock);
}

u8 host_joy_buttons(void) { return joy_present ? joy_buttons : 0; }

/* ---------------------------------------------------------------- audio */

void host_audio_lock(void) { pthread_mutex_lock(&audio_mutex); }
void host_audio_unlock(void) { pthread_mutex_unlock(&audio_mutex); }

void host_opl_write(u8 reg, u8 value)
{
    if (enh_sandbox_deny("opl_write")) return;
    host_audio_lock();
    if (reg == 0x04) {                               /* timer control: model the status flags only */
        if (value & 0x80) opl_status = 0;
        else {
            if ((value & 0x01) && !(value & 0x40)) opl_status |= 0xC0;
            if ((value & 0x02) && !(value & 0x20)) opl_status |= 0xA0;
        }
    }
    int next = (oplq_head + 1) % OPLQ;
    if (next == oplq_tail) {                         /* full (no sound pulled for a while): the oldest goes to the chip now */
        OPL3_WriteReg(&opl, oplq[oplq_tail].reg, oplq[oplq_tail].value);
        oplq_tail = (oplq_tail + 1) % OPLQ;
    }
    uint64_t t = in_tick_ns ? in_tick_ns : clk_ns();
    int last = (oplq_head + OPLQ - 1) % OPLQ;
    if (oplq_head != oplq_tail && t < oplq[last].ns) t = oplq[last].ns;   /* never before an earlier write */
    oplq[oplq_head].ns = t;
    oplq[oplq_head].reg = reg;
    oplq[oplq_head].value = value;
    oplq_head = next;
    host_audio_unlock();
}

u8 host_opl_status(void) { return opl_status; }

/* ---------------------------------------------------------------- files, errors */

static char *path_join(const char *dir, const char *name)
{
    size_t n = strlen(dir) + strlen(name) + 2;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s/%s", dir, name);
    return p;
}

char *host_game_path(const char *name, bool create)
{
    char *direct = path_join(game_dir, name);
    struct stat st;
    if (direct && stat(direct, &st) == 0) return direct;

    char *found = NULL;
    DIR *d = opendir(game_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL)
            if (strcasecmp(e->d_name, name) == 0) { found = path_join(game_dir, e->d_name); break; }
        closedir(d);
    }
    if (found) { free(direct); return found; }
    if (create) return direct;
    free(direct);
    return NULL;
}

const char *host_game_dir(void) { return game_dir; }
void host_free(void *p) { free(p); }

static void log_line(const char *msg)
{
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_ERROR, "F117A-VR", "%s", msg);
#endif
    fprintf(stderr, "f117: %s\n", msg);
    if (!game_dir) return;
    char *path = path_join(game_dir, "f117.log");
    FILE *f = path ? fopen(path, "a") : NULL;
    free(path);
    if (!f) return;
    time_t now = time(NULL);
    char stamp[32] = "";
    struct tm *tm = localtime(&now);
    if (tm) strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", tm);
    fprintf(f, "%s %s\n", stamp, msg);
    fclose(f);
}

void host_log_error(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    log_line(msg);
}

void host_install_crash_log(const char *(*where)(void)) { (void)where; }

/* A fatal error ends the engine thread, not the app: the headset shows the message. */
_Noreturn void host_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(fatal_msg, sizeof fatal_msg, fmt, ap);
    va_end(ap);
    host_log_error("fatal: %s", fatal_msg);
    state = F117VR_FATAL;
    pthread_exit(NULL);
}

/* ---------------------------------------------------------------- f117vr.h */

#define MAX_ARGS 32
static char *argbuf;
static char *argv_[MAX_ARGS + 1];
static int argc_;

static void *engine_thread(void *unused)
{
    (void)unused;
    int rc = f117_main(argc_, argv_);
    host_log_error("chain ended (%d)", rc);
    if (state == F117VR_RUNNING) state = F117VR_FINISHED;
    return NULL;
}

int f117vr_start(const char *dir, const char *args)
{
    if (state == F117VR_RUNNING) return -1;
    size_t n = strlen(dir) + (args ? strlen(args) : 0) + 64;
    free(argbuf);
    argbuf = malloc(n);
    if (!argbuf) return -1;
    snprintf(argbuf, n, "f117 --game-dir %s %s", dir, args ? args : "");
    /* split on spaces; the game folder is the app's own files folder, which has none */
    argc_ = 0;
    for (char *p = strtok(argbuf, " "); p && argc_ < MAX_ARGS; p = strtok(NULL, " ")) argv_[argc_++] = p;
    argv_[argc_] = NULL;

    quit_requested = false;
    state = F117VR_RUNNING;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    int rc = pthread_create(&t, &attr, engine_thread, NULL);
    pthread_attr_destroy(&attr);
    if (rc) { state = F117VR_FATAL; snprintf(fatal_msg, sizeof fatal_msg, "engine thread failed (%d)", rc); return -1; }
    return 0;
}

int f117vr_state(void) { return state; }
const char *f117vr_error(void) { return fatal_msg; }
void f117vr_quit(void) { quit_requested = true; }
const char *f117vr_program(void) { return state == F117VR_RUNNING ? prog_current_name() : ""; }

void f117vr_set_world_mask(int on) { world_mask = on != 0; }

void f117vr_set_view_size(int w, int h)
{
    if (w < 320 || h < 200 || w > 8192 || h > 8192) return;
    pthread_mutex_lock(&io_lock);
    nominal_w = w;
    nominal_h = h;
    pthread_mutex_unlock(&io_lock);
}

int f117vr_frame(uint32_t *dst, int cap, int *w, int *h, float *aspect)
{
    pthread_mutex_lock(&pic_lock);
    int serial = pic_serial;
    if (w) *w = pic_w;
    if (h) *h = pic_h;
    if (aspect) *aspect = pic_aspect;
    if (dst && pic && cap >= pic_w * pic_h) memcpy(dst, pic, (size_t)pic_w * (size_t)pic_h * sizeof(u32));
    pthread_mutex_unlock(&pic_lock);
    return serial;
}

int f117vr_frame_serial(void)
{
    pthread_mutex_lock(&pic_lock);
    int s = pic_serial;
    pthread_mutex_unlock(&pic_lock);
    return s;
}

void f117vr_key(int xt, int down)
{
    pthread_mutex_lock(&io_lock);
    int next = (keyq_head + 1) % KEYQ;
    if (next != keyq_tail) {                         /* a full queue drops the key */
        keyq[keyq_head] = (u16)((xt & 0x1FF) | (down ? 0 : 0x8000));
        keyq_head = next;
    }
    pthread_mutex_unlock(&io_lock);
}

void f117vr_joy(int present, int ax, int ay, int bx, int by, int buttons)
{
    pthread_mutex_lock(&io_lock);
    joy_present = present != 0;
    joy_axes[0] = (s16)ax; joy_axes[1] = (s16)ay; joy_axes[2] = (s16)bx; joy_axes[3] = (s16)by;
    joy_buttons = (u8)(buttons & 0x0F);
    pthread_mutex_unlock(&io_lock);
}

void f117vr_mouse(float u, float v, int buttons)
{
    pthread_mutex_lock(&io_lock);
    if (u != mouse_u || v != mouse_v || (u8)buttons != mouse_buttons) mouse_changed = true;
    mouse_u = u;
    mouse_v = v;
    mouse_buttons = (u8)buttons;
    pthread_mutex_unlock(&io_lock);
}

int f117vr_audio(float *out, int frames)
{
    s16 buf[2 * 512];
    const uint64_t per = 1000000000ull / F117VR_AUDIO_RATE + 1;
    int done = 0;
    host_audio_lock();
    /* the samples are made AUDIO_LAG_NS behind the clock; drifted far from that (the start, a pause), they start over there */
    uint64_t want = clk_ns() - AUDIO_LAG_NS - (uint64_t)frames * 1000000000ull / F117VR_AUDIO_RATE, at = AUDIO_NS();
    if (audio_base_ns == 0 || at + 4 * AUDIO_LAG_NS < want || at > want + 4 * AUDIO_LAG_NS) { audio_base_ns = want; audio_samples = 0; }
    while (done < frames) {
        uint64_t audio_ns = AUDIO_NS();
        while (oplq_tail != oplq_head && oplq[oplq_tail].ns <= audio_ns) {
            OPL3_WriteReg(&opl, oplq[oplq_tail].reg, oplq[oplq_tail].value);
            oplq_tail = (oplq_tail + 1) % OPLQ;
        }
        int n = frames - done < 512 ? frames - done : 512;
        if (oplq_tail != oplq_head) {                /* up to the next write's sample */
            uint64_t to = (oplq[oplq_tail].ns - audio_ns + per - 1) / per;
            if (to < (uint64_t)n) n = to < 1 ? 1 : (int)to;
        }
        OPL3_GenerateStream(&opl, buf, (uint32_t)n);
        for (int i = 0; i < n; i++) out[done + i] = ((float)buf[2 * i] + (float)buf[2 * i + 1]) * (0.5f / 32768.0f);
        done += n;
        audio_samples += (uint64_t)n;
    }
    host_audio_unlock();
    return frames;
}

void f117vr_world_request(void) { vr_world_request(); }
int  f117vr_world_serial(void) { return vr_world_serial(); }
void f117vr_world_counts(int *nmesh, int *ntri, int *nline, int *ninst) { vr_world_counts(nmesh, ntri, nline, ninst); }
int  f117vr_world_copy(void *m, int cm, void *t, int ct, void *l, int cl, void *in, int ci)
{
    return vr_world_copy(m, cm, t, ct, l, cl, in, ci);
}
void f117vr_camera(void *out) { vr_camera_get(out); }
void f117vr_palette(uint8_t *rgb768) { vr_palette_get(rgb768); }
int  f117vr_objects(void *out, int cap) { return vr_objects_get(out, cap); }
int  f117vr_sim_frame(void *c, void *out, int cap) { return vr_frame_get(c, out, cap); }
long long f117vr_now_ns(void) { return (long long)mono_ns(); }
void f117vr_fdm_enable(int on) { vr_fdm_enable(on); }
void f117vr_fdm_set(const void *s) { vr_fdm_set(s); }
void f117vr_fdm_game(void *g) { vr_fdm_game(g); }
int  f117vr_world_live(void *in, int ci, int known) { return vr_world_live(in, ci, known); }
int  f117vr_events(void *out, int cap, int after) { return vr_events_get(out, cap, after); }
