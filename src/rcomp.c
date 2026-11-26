/*
 * rcomp.c - minimal X11 compositing manager
 *
 * Features
 *   - XComposite redirection, XDamage-driven *region* repaint (only damaged
 *     areas are redrawn), XRender compositing onto the composite overlay
 *   - TOML configuration with live reload (inotify on the config directory,
 *     or `kill -USR1 <pid>`)
 *   - gaussian drop shadows (exact, separable), body cut out of shadows of
 *     translucent windows
 *   - fade in / fade out, smooth opacity transitions
 *   - active / inactive window opacity (_NET_ACTIVE_WINDOW),
 *     _NET_WM_WINDOW_OPACITY support
 *   - per-window rules by WM_CLASS and _NET_WM_WINDOW_TYPE
 *   - frame limiter, clean shutdown on SIGINT/SIGTERM, exits if another
 *     compositing manager takes over the selection
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o rcomp rcomp.c \
 *       -lX11 -lXcomposite -lXdamage -lXfixes -lXrender -lm
 *
 * Run (inside a running X session with a normal window manager):
 *   ./rcomp [-c config.toml] [-v]
 *   ./rcomp --dump-config > config.toml
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_CONFIG "/root/.config/rcomp/config.toml"
#define MAX_RULES 64

/* ===================================================================== */
/*  Logging                                                              */
/* ===================================================================== */

static int verbose;

static void vlog(const char *lvl, const char *fmt, va_list ap)
{
    fprintf(stderr, "rcomp: %s: ", lvl);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

static void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void log_info(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vlog("info", fmt, ap); va_end(ap);
}
static void log_warn(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vlog("warning", fmt, ap); va_end(ap);
}
static void log_dbg(const char *fmt, ...)
{
    if (!verbose) return;
    va_list ap; va_start(ap, fmt); vlog("debug", fmt, ap); va_end(ap);
}

/* ===================================================================== */
/*  Configuration                                                        */
/* ===================================================================== */

enum {
    T_UNKNOWN, T_NORMAL, T_DESKTOP, T_DOCK, T_TOOLBAR, T_MENU, T_UTILITY,
    T_SPLASH, T_DIALOG, T_DROPDOWN, T_POPUP, T_TOOLTIP, T_NOTIFY, T_COMBO,
    T_DND, T_COUNT
};

static const char *const type_names[T_COUNT] = {
    "unknown", "normal", "desktop", "dock", "toolbar", "menu", "utility",
    "splash", "dialog", "dropdown_menu", "popup_menu", "tooltip",
    "notification", "combo", "dnd"
};

typedef struct {
    char   cls[64];     /* WM_CLASS instance or class, case-insensitive; "" = any */
    int    type;        /* window type, -1 = any */
    double opacity;     /* < 0 = not set */
    int    shadow;      /* -1 = not set */
    int    fade;        /* -1 = not set */
} Rule;

typedef struct {
    int    fps;
    double bg[3];
    int    shadow, sh_radius, sh_dx, sh_dy;
    double sh_opacity, sh_rgb[3];
    double op_active, op_inactive, op_transition_ms;
    int    op_respect_prop;
    int    fade;
    double fade_in_ms, fade_out_ms;
    int    nrules;
    Rule   rules[MAX_RULES];
} Config;

static const char default_config[] =
"# rcomp \xE2\x80\x94 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x84\xD0\xB8\xD0\xB3\xD1\x83\xD1\x80\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F (TOML)\n"
"# \xD0\xA4\xD0\xB0\xD0\xB9\xD0\xBB \xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD1\x87\xD0\xB8\xD1\x82\xD1\x8B\xD0\xB2\xD0\xB0\xD0\xB5\xD1\x82\xD1\x81\xD1\x8F \xD0\xB0\xD0\xB2\xD1\x82\xD0\xBE\xD0\xBC\xD0\xB0\xD1\x82\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0\xBA\xD0\xB8 \xD0\xBF\xD1\x80\xD0\xB8 \xD1\x81\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB8; \xD0\xB8\xD0\xBB\xD0\xB8 kill -USR1 <pid>.\n"
"\n"
"[general]\n"
"fps = 60                  # max repaint rate (1..240)\n"
"background = \"#181820\"    # colour where no window is drawn\n"
"\n"
"[shadow]\n"
"enabled = true\n"
"radius = 18               # blur extent in pixels (1..128)\n"
"offset_x = 0\n"
"offset_y = 8\n"
"opacity = 0.5             # peak shadow opacity (0..1)\n"
"color = \"#000000\"\n"
"\n"
"[opacity]\n"
"active = 1.0              # focused window\n"
"inactive = 0.95           # unfocused normal/dialog/utility windows\n"
"respect_property = true   # honour _NET_WM_WINDOW_OPACITY (transset etc.)\n"
"transition_ms = 120       # smooth opacity changes, 0 = instant\n"
"\n"
"[fade]\n"
"enabled = true\n"
"in_ms = 150\n"
"out_ms = 120\n"
"\n"
"# Window rules. Later rules override earlier ones.\n"
"#   match:   class = \"...\" (WM_CLASS instance or class, case-insensitive)\n"
"#            type  = \"...\" (normal, dialog, dock, desktop, toolbar, menu,\n"
"#                           utility, splash, dropdown_menu, popup_menu,\n"
"#                           tooltip, notification, combo, dnd, unknown)\n"
"#   actions: opacity = 0.0..1.0, shadow = true/false, fade = true/false\n"
"[[rules]]\n"
"type = \"desktop\"\n"
"shadow = false\n"
"\n"
"[[rules]]\n"
"type = \"dock\"\n"
"shadow = false\n"
"\n"
"# [[rules]]\n"
"# class = \"firefox\"\n"
"# opacity = 1.0\n";

static double clampd(double x, double lo, double hi)
{
    if (!(x >= lo)) return lo;      /* also catches NaN */
    if (x > hi) return hi;
    return x;
}

static int clampi(int x, int lo, int hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static void config_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    c->fps = 60;
    c->bg[0] = 0x18 / 255.0; c->bg[1] = 0x18 / 255.0; c->bg[2] = 0x20 / 255.0;
    c->shadow = 1; c->sh_radius = 18; c->sh_dx = 0; c->sh_dy = 8;
    c->sh_opacity = 0.5;
    c->op_active = 1.0; c->op_inactive = 0.95; c->op_transition_ms = 120;
    c->op_respect_prop = 1;
    c->fade = 1; c->fade_in_ms = 150; c->fade_out_ms = 120;
}

static void config_validate(Config *c)
{
    c->fps = clampi(c->fps, 1, 240);
    c->sh_radius = clampi(c->sh_radius, 1, 128);
    c->sh_dx = clampi(c->sh_dx, -200, 200);
    c->sh_dy = clampi(c->sh_dy, -200, 200);
    c->sh_opacity = clampd(c->sh_opacity, 0, 1);
    c->op_active = clampd(c->op_active, 0, 1);
    c->op_inactive = clampd(c->op_inactive, 0, 1);
    c->op_transition_ms = clampd(c->op_transition_ms, 0, 5000);
    c->fade_in_ms = clampd(c->fade_in_ms, 0, 5000);
    c->fade_out_ms = clampd(c->fade_out_ms, 0, 5000);
}

/* ---- minimal TOML subset: [table], [[rules]], key = string|bool|number ---- */

typedef struct {
    enum { V_NONE, V_STR, V_BOOL, V_INT, V_FLT } t;
    char   s[256];
    int    b;
    double f;
} Val;

enum { S_NONE, S_GENERAL, S_SHADOW, S_OPACITY, S_FADE, S_RULE };

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void strip_comment(char *s)
{
    char q = 0;
    for (; *s; s++) {
        if (q) {
            if (q == '"' && *s == '\\' && s[1]) s++;
            else if (*s == q) q = 0;
        } else if (*s == '"' || *s == '\'') {
            q = *s;
        } else if (*s == '#') {
            *s = 0;
            return;
        }
    }
}

static void copy_str(char *dst, size_t n, const char *src)
{
    size_t l = strlen(src);
    if (l >= n) l = n - 1;
    memcpy(dst, src, l);
    dst[l] = 0;
}

static int parse_value(const char *s, Val *v)
{
    memset(v, 0, sizeof *v);

    if (*s == '"') {
        size_t n = 0;
        s++;
        while (*s && *s != '"') {
            char c = *s++;
            if (c == '\\') {
                c = *s;
                if (!c) return -1;
                s++;
                switch (c) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case '"': case '\\': break;
                default: return -1;
                }
            }
            if (n + 1 < sizeof v->s) v->s[n++] = c;
        }
        if (*s != '"' || s[1]) return -1;
        v->t = V_STR;
        return 0;
    }
    if (*s == '\'') {
        const char *e = strchr(s + 1, '\'');
        if (!e || e[1]) return -1;
        size_t n = (size_t)(e - (s + 1));
        if (n >= sizeof v->s) n = sizeof v->s - 1;
        memcpy(v->s, s + 1, n);
        v->t = V_STR;
        return 0;
    }
    if (!strcmp(s, "true"))  { v->t = V_BOOL; v->b = 1; return 0; }
    if (!strcmp(s, "false")) { v->t = V_BOOL; v->b = 0; return 0; }

    char buf[64];
    size_t n = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '_') continue;
        if (n + 1 >= sizeof buf) return -1;
        buf[n++] = *p;
    }
    buf[n] = 0;
    if (!n) return -1;

    char *end;
    if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        long l = strtol(buf, &end, 16);
        if (end == buf || *end) return -1;
        v->t = V_INT; v->f = (double)l;
        return 0;
    }
    double d = strtod(buf, &end);
    if (end == buf || *end) return -1;
    v->f = d;
    v->t = strpbrk(buf, ".eE") ? V_FLT : V_INT;
    return 0;
}

static int want_bool(const Val *v, int *o)
{
    if (v->t != V_BOOL) return -1;
    *o = v->b;
    return 0;
}

static int want_num(const Val *v, double *o)
{
    if (v->t != V_INT && v->t != V_FLT) return -1;
    *o = v->f;
    return 0;
}

static int want_int(const Val *v, int *o)
{
    double d;
    if (want_num(v, &d)) return -1;
    *o = (int)clampd(d, -1e9, 1e9);
    return 0;
}

static int want_color(const Val *v, double o[3])
{
    unsigned r, g, b;
    if (v->t != V_STR || strlen(v->s) != 7 || v->s[0] != '#') return -1;
    if (sscanf(v->s + 1, "%2x%2x%2x", &r, &g, &b) != 3) return -1;
    o[0] = r / 255.0; o[1] = g / 255.0; o[2] = b / 255.0;
    return 0;
}

static int type_from_name(const char *s)
{
    for (int i = 0; i < T_COUNT; i++)
        if (!strcasecmp(s, type_names[i])) return i;
    return -1;
}

static int eq(const char *a, const char *b) { return !strcmp(a, b); }

/* returns 0 = ok, -1 = bad value/type, 1 = unknown key */
static int apply_kv(Config *c, int sec, const char *k, const Val *v)
{
    Rule *r = c->nrules ? &c->rules[c->nrules - 1] : NULL;

    switch (sec) {
    case S_GENERAL:
        if (eq(k, "fps"))        return want_int(v, &c->fps);
        if (eq(k, "background")) return want_color(v, c->bg);
        break;
    case S_SHADOW:
        if (eq(k, "enabled"))    return want_bool(v, &c->shadow);
        if (eq(k, "radius"))     return want_int(v, &c->sh_radius);
        if (eq(k, "offset_x"))   return want_int(v, &c->sh_dx);
        if (eq(k, "offset_y"))   return want_int(v, &c->sh_dy);
        if (eq(k, "opacity"))    return want_num(v, &c->sh_opacity);
        if (eq(k, "color"))      return want_color(v, c->sh_rgb);
        break;
    case S_OPACITY:
        if (eq(k, "active"))           return want_num(v, &c->op_active);
        if (eq(k, "inactive"))         return want_num(v, &c->op_inactive);
        if (eq(k, "respect_property")) return want_bool(v, &c->op_respect_prop);
        if (eq(k, "transition_ms"))    return want_num(v, &c->op_transition_ms);
        break;
    case S_FADE:
        if (eq(k, "enabled")) return want_bool(v, &c->fade);
        if (eq(k, "in_ms"))   return want_num(v, &c->fade_in_ms);
        if (eq(k, "out_ms"))  return want_num(v, &c->fade_out_ms);
        break;
    case S_RULE:
        if (!r) return 1;
        if (eq(k, "class")) {
            if (v->t != V_STR) return -1;
            copy_str(r->cls, sizeof r->cls, v->s);
            return 0;
        }
        if (eq(k, "type")) {
            if (v->t != V_STR) return -1;
            int t = type_from_name(v->s);
            if (t < 0) return -1;
            r->type = t;
            return 0;
        }
        if (eq(k, "opacity")) {
            double d;
            if (want_num(v, &d)) return -1;
            r->opacity = clampd(d, 0, 1);
            return 0;
        }
        if (eq(k, "shadow")) return want_bool(v, &r->shadow);
        if (eq(k, "fade"))   return want_bool(v, &r->fade);
        break;
    }
    return 1;
}

static void rule_init(Rule *r)
{
    r->cls[0] = 0;
    r->type = -1;
    r->opacity = -1;
    r->shadow = -1;
    r->fade = -1;
}

/* 0 = loaded (possibly with warnings), -1 = cannot open */
static int config_load(const char *path, Config *c)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char *line = NULL;
    size_t cap = 0;
    int lineno = 0, sec = S_NONE;

    while (getline(&line, &cap, f) >= 0) {
        lineno++;
        strip_comment(line);
        char *s = trim(line);
        if (!*s) continue;

        if (*s == '[') {
            int dbl = (s[1] == '[');
            char *name = s + 1 + dbl;
            char *end = strchr(name, ']');
            if (!end || (dbl && end[1] != ']') || end[1 + dbl]) {
                log_warn("%s:%d: malformed table header", path, lineno);
                sec = S_NONE;
                continue;
            }
            *end = 0;
            name = trim(name);
            if (dbl) {
                if (eq(name, "rules")) {
                    if (c->nrules >= MAX_RULES) {
                        log_warn("%s:%d: too many rules (max %d)", path, lineno, MAX_RULES);
                        sec = S_NONE;
                    } else {
                        rule_init(&c->rules[c->nrules++]);
                        sec = S_RULE;
                    }
                } else {
                    log_warn("%s:%d: unknown array of tables [[%s]]", path, lineno, name);
                    sec = S_NONE;
                }
            } else if (eq(name, "general")) sec = S_GENERAL;
            else if (eq(name, "shadow"))    sec = S_SHADOW;
            else if (eq(name, "opacity"))   sec = S_OPACITY;
            else if (eq(name, "fade"))      sec = S_FADE;
            else {
                log_warn("%s:%d: unknown section [%s]", path, lineno, name);
                sec = S_NONE;
            }
            continue;
        }

        char *eqp = strchr(s, '=');
        if (!eqp) {
            log_warn("%s:%d: expected `key = value`", path, lineno);
            continue;
        }
        *eqp = 0;
        char *key = trim(s), *val = trim(eqp + 1);
        Val v;
        if (!*key || parse_value(val, &v)) {
            log_warn("%s:%d: cannot parse value of `%s`", path, lineno, key);
            continue;
        }
        int rc = apply_kv(c, sec, key, &v);
        if (rc > 0)      log_warn("%s:%d: unknown key `%s`", path, lineno, key);
        else if (rc < 0) log_warn("%s:%d: invalid value for `%s`", path, lineno, key);
    }
    free(line);
    fclose(f);
    config_validate(c);
    return 0;
}

/* ===================================================================== */
/*  Global state                                                         */
/* ===================================================================== */

typedef struct Win {
    Window id;
    Window client;              /* window carrying WM_STATE (the real app window) */
    XWindowAttributes a;
    Damage damage;
    Picture pic;                /* picture of the window's offscreen pixmap */
    Picture shadow_pic;         /* A8 shadow mask */
    int shadow_w, shadow_h, shadow_r;   /* cache key of shadow_pic */
    double cur, tgt, rate;      /* opacity animation: current, target, per-ms speed */
    double prop_opacity;        /* _NET_WM_WINDOW_OPACITY, < 0 = unset */
    double rule_opacity;        /* from rules, < 0 = unset */
    int rule_shadow, rule_fade; /* from rules, -1 = unset */
    int wtype;
    int mapped, fading, destroyed, has_alpha;
    XRenderPictFormat *fmt;
    struct Win *next;           /* list is ordered bottom -> top */
} Win;

static Display *dpy;
static int scr;
static Window root, overlay, sel_win;
static Picture root_pic, buf_pic;
static Pixmap buf;
static XRenderPictFormat *root_fmt;
static int root_w, root_h;
static int damage_ev, damage_err;
static Atom a_opacity, a_active, a_wm_state, a_type;
static Atom type_atoms[T_COUNT];
static Win *wins;
static XserverRegion damage;        /* accumulated screen damage, None = clean */
static Window active_win;
static Config cfg;
static char cfg_path[PATH_MAX];
static char cfg_dir[PATH_MAX];
static const char *cfg_base;
static int running = 1;
static int reload_requested;
static int sigpipe[2] = { -1, -1 };
static int ifd = -1;
static double frame_ms = 1000.0 / 60.0;

static int x_error(Display *d, XErrorEvent *e)
{
    if (verbose) {
        char msg[128];
        XGetErrorText(d, e->error_code, msg, sizeof msg);
        log_dbg("X error: %s (request %d.%d, resource 0x%lx)",
                msg, e->request_code, e->minor_code, e->resourceid);
    }
    return 0;           /* windows vanish all the time; ignore races */
}

static double mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ===================================================================== */
/*  Window list                                                          */
/* ===================================================================== */

static Win *find_win(Window id)
{
    for (Win *w = wins; w; w = w->next)
        if (w->id == id) return w;
    return NULL;
}

static Win *find_win_by_client(Window id)
{
    for (Win *w = wins; w; w = w->next)
        if (w->client == id) return w;
    return NULL;
}

static void unlink_win(Win *w)
{
    for (Win **pp = &wins; *pp; pp = &(*pp)->next)
        if (*pp == w) { *pp = w->next; w->next = NULL; return; }
}

static void append_win(Win *w)
{
    Win **pp = &wins;
    while (*pp) pp = &(*pp)->next;
    *pp = w;
    w->next = NULL;
}

/* place w directly above `above` (None = bottom of the stack) */
static void restack(Win *w, Window above)
{
    unlink_win(w);
    if (!above) { w->next = wins; wins = w; return; }
    Win *a = find_win(above);
    if (!a) { append_win(w); return; }
    w->next = a->next;
    a->next = w;
}

static int visible(const Win *w) { return w->mapped || w->fading; }

/* ===================================================================== */
/*  Damage tracking                                                      */
/* ===================================================================== */

static void add_damage(XserverRegion r)
{
    if (!damage) { damage = r; return; }
    XFixesUnionRegion(dpy, damage, damage, r);
    XFixesDestroyRegion(dpy, r);
}

static void damage_rect(int x, int y, int w, int h)
{
    int x2 = x + w, y2 = y + h;
    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    if (x2 > root_w) { x2 = root_w; }
    if (y2 > root_h) { y2 = root_h; }
    if (x2 <= x || y2 <= y) return;
    XRectangle r = { (short)x, (short)y,
                     (unsigned short)(x2 - x), (unsigned short)(y2 - y) };
    add_damage(XFixesCreateRegion(dpy, &r, 1));
}

static void damage_all(void) { damage_rect(0, 0, root_w, root_h); }

static int win_w(const Win *w) { return w->a.width  + 2 * w->a.border_width; }
static int win_h(const Win *w) { return w->a.height + 2 * w->a.border_width; }

static int shadow_on(const Win *w)
{
    int on = w->rule_shadow >= 0 ? w->rule_shadow : cfg.shadow;
    if (!on) return 0;
    if (win_w(w) >= root_w && win_h(w) >= root_h) return 0;     /* fullscreen */
    return 1;
}

/* mark the window (and its shadow) for repaint */
static void damage_win(const Win *w)
{
    int x1 = w->a.x, y1 = w->a.y;
    int x2 = x1 + win_w(w), y2 = y1 + win_h(w);
    if (shadow_on(w)) {
        int r = cfg.sh_radius;
        int sx = w->a.x + cfg.sh_dx - r, sy = w->a.y + cfg.sh_dy - r;
        int ex = sx + win_w(w) + 2 * r, ey = sy + win_h(w) + 2 * r;
        if (sx < x1) { x1 = sx; }
        if (sy < y1) { y1 = sy; }
        if (ex > x2) { x2 = ex; }
        if (ey > y2) { y2 = ey; }
    }
    damage_rect(x1, y1, x2 - x1, y2 - y1);
}

/* ===================================================================== */
/*  Window properties and classification                                 */
/* ===================================================================== */

static double read_opacity(Window id)
{
    Atom type; int fmt; unsigned long n, after;
    unsigned char *data = NULL;
    double op = -1.0;

    if (XGetWindowProperty(dpy, id, a_opacity, 0, 1, False, XA_CARDINAL,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        if (type == XA_CARDINAL && fmt == 32 && n == 1)
            op = (double)(*(unsigned long *)data & 0xffffffffUL) / 4294967295.0;
        XFree(data);
    }
    return op;
}

static Window read_active(void)
{
    Atom type; int fmt; unsigned long n, after;
    unsigned char *data = NULL;
    Window a = None;

    if (XGetWindowProperty(dpy, root, a_active, 0, 1, False, XA_WINDOW,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        if (type == XA_WINDOW && fmt == 32 && n == 1)
            a = *(Window *)data;
        XFree(data);
    }
    return a;
}

static int read_type(Window id)
{
    Atom type; int fmt; unsigned long n, after;
    unsigned char *data = NULL;
    int res = -1;

    if (XGetWindowProperty(dpy, id, a_type, 0, 16, False, XA_ATOM,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        if (type == XA_ATOM && fmt == 32) {
            Atom *atoms = (Atom *)data;
            for (unsigned long i = 0; i < n && res < 0; i++)
                for (int t = 0; t < T_COUNT; t++)
                    if (type_atoms[t] && type_atoms[t] == atoms[i]) { res = t; break; }
        }
        XFree(data);
    }
    return res;
}

/* find the window below `w` that carries WM_STATE (ICCCM client window) */
static Window find_client(Window w, int depth)
{
    Atom type; int fmt; unsigned long n, after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, w, a_wm_state, 0, 0, False, AnyPropertyType,
                           &type, &fmt, &n, &after, &data) == Success) {
        if (data) XFree(data);
        if (type != None) return w;
    }
    if (depth <= 0) return None;

    Window r, p, *ch = NULL, found = None;
    unsigned int nch = 0;
    if (XQueryTree(dpy, w, &r, &p, &ch, &nch)) {
        for (unsigned int i = 0; i < nch && !found; i++)
            found = find_client(ch[i], depth - 1);
        if (ch) XFree(ch);
    }
    return found;
}

static int inactive_applies(int t)
{
    return t == T_NORMAL || t == T_DIALOG || t == T_UTILITY;
}

static int is_active(const Win *w)
{
    return active_win && (w->id == active_win ||
                          (w->client && w->client == active_win));
}

static double target_opacity(const Win *w)
{
    if (w->rule_opacity >= 0) return w->rule_opacity;
    if (cfg.op_respect_prop && w->prop_opacity >= 0) return w->prop_opacity;
    if (active_win && !is_active(w) && inactive_applies(w->wtype))
        return cfg.op_inactive;
    return cfg.op_active;
}

static int fade_allowed(const Win *w)
{
    return w->rule_fade >= 0 ? w->rule_fade : cfg.fade;
}

/* (re)read class/type/opacity of a window and apply the rules */
static void reclassify(Win *w)
{
    if (!w->client && !w->a.override_redirect)
        w->client = find_client(w->id, 3);
    if (w->client && w->client != w->id)
        XSelectInput(dpy, w->client, PropertyChangeMask);

    Window src = w->client ? w->client : w->id;

    int t = read_type(src);
    if (t < 0 && src != w->id) t = read_type(w->id);
    w->wtype = t >= 0 ? t : (w->client ? T_NORMAL : T_UNKNOWN);

    w->prop_opacity = read_opacity(w->id);
    if (w->prop_opacity < 0 && src != w->id)
        w->prop_opacity = read_opacity(src);

    XClassHint ch = { NULL, NULL };
    if (!XGetClassHint(dpy, src, &ch) && src != w->id)
        XGetClassHint(dpy, w->id, &ch);

    w->rule_opacity = -1;
    w->rule_shadow = -1;
    w->rule_fade = -1;
    for (int i = 0; i < cfg.nrules; i++) {
        const Rule *r = &cfg.rules[i];
        if (r->type >= 0 && r->type != w->wtype) continue;
        if (r->cls[0]) {
            int m = (ch.res_name  && !strcasecmp(ch.res_name,  r->cls)) ||
                    (ch.res_class && !strcasecmp(ch.res_class, r->cls));
            if (!m) continue;
        }
        if (r->opacity >= 0) w->rule_opacity = r->opacity;
        if (r->shadow >= 0)  w->rule_shadow = r->shadow;
        if (r->fade >= 0)    w->rule_fade = r->fade;
    }
    log_dbg("window 0x%lx: class=%s/%s type=%s client=0x%lx",
            w->id, ch.res_name ? ch.res_name : "-",
            ch.res_class ? ch.res_class : "-", type_names[w->wtype], w->client);
    if (ch.res_name)  XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
}

/* ===================================================================== */
/*  Opacity animation                                                    */
/* ===================================================================== */

static void set_target(Win *w, double t, double ms)
{
    w->tgt = t;
    if (ms <= 0) {
        if (w->cur != t) { w->cur = t; damage_win(w); }
    } else {
        w->rate = 1.0 / ms;
    }
}

/* the desired opacity may have changed (focus, property, rules, config) */
static void retarget(Win *w)
{
    if (!w->mapped) return;
    double t = target_opacity(w);
    if (t == w->tgt) return;
    set_target(w, t, cfg.op_transition_ms);
}

static int any_animating(void)
{
    for (Win *w = wins; w; w = w->next)
        if (w->cur != w->tgt) return 1;
    return 0;
}

/* ===================================================================== */
/*  Pictures                                                             */
/* ===================================================================== */

static void free_pic(Win *w)
{
    if (w->pic) { XRenderFreePicture(dpy, w->pic); w->pic = 0; }
}

static void free_shadow(Win *w)
{
    if (w->shadow_pic) { XRenderFreePicture(dpy, w->shadow_pic); w->shadow_pic = 0; }
    w->shadow_w = w->shadow_h = w->shadow_r = 0;
}

static void ensure_pic(Win *w)
{
    if (w->pic || !w->mapped || !w->fmt) return;
    Pixmap p = XCompositeNameWindowPixmap(dpy, w->id);
    if (!p) return;
    w->pic = XRenderCreatePicture(dpy, p, w->fmt, 0, NULL);
    XFreePixmap(dpy, p);    /* picture keeps its own reference */
}

/*
 * Shadow of a rectangle blurred with a gaussian is separable:
 *   a(x, y) = fx(x) * fy(y),  f(u) = (erf(u / s*sqrt2) - erf((u - len) / s*sqrt2)) / 2
 * so it is exact and costs O(w*h) multiplies, no convolution.
 */
static void ensure_shadow(Win *w)
{
    int r = cfg.sh_radius;
    int ww = win_w(w), wh = win_h(w);
    int sw = ww + 2 * r, sh = wh + 2 * r;

    if (w->shadow_pic && w->shadow_w == sw && w->shadow_h == sh && w->shadow_r == r)
        return;
    free_shadow(w);
    if (ww <= 0 || wh <= 0 || sw > 32767 || sh > 32767 ||
        (long)sw * sh > 48L * 1024 * 1024)
        return;

    double sigma = r / 2.5;
    double k = 1.0 / (sigma * M_SQRT2);
    double *fx = malloc((size_t)sw * sizeof *fx);
    double *fy = malloc((size_t)sh * sizeof *fy);
    unsigned char *data = malloc((size_t)sw * sh);
    if (!fx || !fy || !data) { free(fx); free(fy); free(data); return; }

    for (int i = 0; i < sw; i++) {
        double u = i + 0.5 - r;
        fx[i] = 0.5 * (erf(u * k) - erf((u - ww) * k));
    }
    for (int i = 0; i < sh; i++) {
        double u = i + 0.5 - r;
        fy[i] = 0.5 * (erf(u * k) - erf((u - wh) * k));
    }
    for (int y = 0; y < sh; y++) {
        unsigned char *row = data + (size_t)y * sw;
        for (int x = 0; x < sw; x++)
            row[x] = (unsigned char)(clampd(fx[x] * fy[y], 0, 1) * 255.0 + 0.5);
    }
    free(fx);
    free(fy);

    Pixmap pm = XCreatePixmap(dpy, root, sw, sh, 8);
    GC gc = XCreateGC(dpy, pm, 0, NULL);
    XImage *img = XCreateImage(dpy, DefaultVisual(dpy, scr), 8, ZPixmap, 0,
                               (char *)data, sw, sh, 8, sw);
    if (!img) {
        free(data);
    } else {
        XPutImage(dpy, pm, gc, img, 0, 0, 0, 0, sw, sh);
        XDestroyImage(img);         /* also frees data */
        w->shadow_pic = XRenderCreatePicture(dpy, pm,
                            XRenderFindStandardFormat(dpy, PictStandardA8), 0, NULL);
        w->shadow_w = sw; w->shadow_h = sh; w->shadow_r = r;
    }
    XFreeGC(dpy, gc);
    XFreePixmap(dpy, pm);
}

static void make_buffer(void)
{
    if (buf_pic) XRenderFreePicture(dpy, buf_pic);
    if (buf) XFreePixmap(dpy, buf);
    buf = XCreatePixmap(dpy, root, root_w, root_h, DefaultDepth(dpy, scr));
    buf_pic = XRenderCreatePicture(dpy, buf, root_fmt, 0, NULL);
}

/* ===================================================================== */
/*  Window lifecycle                                                     */
/* ===================================================================== */

static void add_win(Window id)
{
    if (id == overlay || id == sel_win || find_win(id)) return;

    Win *w = calloc(1, sizeof *w);
    if (!w) return;
    w->id = id;
    if (!XGetWindowAttributes(dpy, id, &w->a) || w->a.class == InputOnly) {
        free(w);
        return;
    }
    w->fmt = XRenderFindVisualFormat(dpy, w->a.visual);
    w->has_alpha = w->fmt && w->fmt->type == PictTypeDirect &&
                   w->fmt->direct.alphaMask;
    w->mapped = (w->a.map_state == IsViewable);
    w->prop_opacity = w->rule_opacity = -1;
    w->rule_shadow = w->rule_fade = -1;
    w->wtype = T_UNKNOWN;
    w->damage = XDamageCreate(dpy, id, XDamageReportNonEmpty);
    XSelectInput(dpy, id, PropertyChangeMask);
    append_win(w);

    if (w->mapped) {                    /* already on screen: no fade-in */
        reclassify(w);
        w->cur = w->tgt = target_opacity(w);
        damage_win(w);
    }
}

static void remove_win(Win *w)
{
    damage_win(w);
    unlink_win(w);
    free_pic(w);
    free_shadow(w);
    if (w->damage) XDamageDestroy(dpy, w->damage);
    free(w);
}

/* fade-out finished */
static void retire(Win *w)
{
    free_pic(w);
    free_shadow(w);
    if (w->destroyed) remove_win(w);
}

static void tick(double dt)
{
    Win *w, *nx;
    for (w = wins; w; w = nx) {
        nx = w->next;
        if (w->cur == w->tgt) continue;
        damage_win(w);
        double step = w->rate * dt;
        if (w->cur < w->tgt) {
            w->cur += step;
            if (w->cur > w->tgt) w->cur = w->tgt;
        } else {
            w->cur -= step;
            if (w->cur < w->tgt) w->cur = w->tgt;
        }
        if (w->fading && w->cur <= 0.0) {
            w->fading = 0;
            retire(w);
        }
    }
}

/* ===================================================================== */
/*  Painting                                                             */
/* ===================================================================== */

static XRenderColor premul(const double rgb[3], double a)
{
    XRenderColor c;
    c.red   = (unsigned short)(rgb[0] * a * 0xffff);
    c.green = (unsigned short)(rgb[1] * a * 0xffff);
    c.blue  = (unsigned short)(rgb[2] * a * 0xffff);
    c.alpha = (unsigned short)(a * 0xffff);
    return c;
}

static void paint(void)
{
    if (!damage) return;

    XFixesSetPictureClipRegion(dpy, buf_pic, 0, 0, damage);

    XRenderColor bg = premul(cfg.bg, 1.0);
    XRenderFillRectangle(dpy, PictOpSrc, buf_pic, &bg, 0, 0, root_w, root_h);

    for (Win *w = wins; w; w = w->next) {
        if (!visible(w) || w->cur <= 0.001) continue;
        ensure_pic(w);
        if (!w->pic) continue;

        int x = w->a.x, y = w->a.y, ww = win_w(w), wh = win_h(w);
        double o = w->cur;
        int opaque = (o >= 0.999 && !w->has_alpha);

        /* shadow */
        if (shadow_on(w)) {
            ensure_shadow(w);
            if (w->shadow_pic) {
                XserverRegion tmp = None;
                if (!opaque) {          /* see-through body: cut it out of the shadow */
                    int bx = clampi(x, -32768, 32767), by = clampi(y, -32768, 32767);
                    XRectangle br = { (short)bx, (short)by,
                                      (unsigned short)clampi(ww, 0, 65535),
                                      (unsigned short)clampi(wh, 0, 65535) };
                    XserverRegion body = XFixesCreateRegion(dpy, &br, 1);
                    tmp = XFixesCreateRegion(dpy, NULL, 0);
                    XFixesCopyRegion(dpy, tmp, damage);
                    XFixesSubtractRegion(dpy, tmp, tmp, body);
                    XFixesDestroyRegion(dpy, body);
                    XFixesSetPictureClipRegion(dpy, buf_pic, 0, 0, tmp);
                }
                XRenderColor sc = premul(cfg.sh_rgb, cfg.sh_opacity * o);
                Picture src = XRenderCreateSolidFill(dpy, &sc);
                XRenderComposite(dpy, PictOpOver, src, w->shadow_pic, buf_pic,
                                 0, 0, 0, 0,
                                 x + cfg.sh_dx - cfg.sh_radius,
                                 y + cfg.sh_dy - cfg.sh_radius,
                                 w->shadow_w, w->shadow_h);
                XRenderFreePicture(dpy, src);
                if (tmp) {
                    XFixesSetPictureClipRegion(dpy, buf_pic, 0, 0, damage);
                    XFixesDestroyRegion(dpy, tmp);
                }
            }
        }

        /* window contents */
        if (opaque) {
            XRenderComposite(dpy, PictOpSrc, w->pic, None, buf_pic,
                             0, 0, 0, 0, x, y, ww, wh);
        } else {
            Picture mask = None;
            if (o < 0.999) {
                XRenderColor c = { 0, 0, 0, (unsigned short)(o * 0xffff) };
                mask = XRenderCreateSolidFill(dpy, &c);
            }
            XRenderComposite(dpy, PictOpOver, w->pic, mask, buf_pic,
                             0, 0, 0, 0, x, y, ww, wh);
            if (mask) XRenderFreePicture(dpy, mask);
        }
    }

    XFixesSetPictureClipRegion(dpy, root_pic, 0, 0, damage);
    XRenderComposite(dpy, PictOpSrc, buf_pic, None, root_pic,
                     0, 0, 0, 0, 0, 0, root_w, root_h);
    XFixesDestroyRegion(dpy, damage);
    damage = None;
    XFlush(dpy);
}

/* ===================================================================== */
/*  Config reload                                                        */
/* ===================================================================== */

static void reload_config(void)
{
    Config nc;
    config_defaults(&nc);
    if (config_load(cfg_path, &nc) < 0) {
        log_warn("cannot read %s: %s (keeping current settings)",
                 cfg_path, strerror(errno));
        return;
    }
    cfg = nc;
    frame_ms = 1000.0 / cfg.fps;

    for (Win *w = wins; w; w = w->next) {
        if (!w->mapped) continue;
        reclassify(w);
        retarget(w);
    }
    damage_all();
    log_info("configuration loaded from %s", cfg_path);
}

static void mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    size_t n = strlen(path);
    if (n >= sizeof tmp) return;
    memcpy(tmp, path, n + 1);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static void write_default_config(void)
{
    copy_str(cfg_dir, sizeof cfg_dir, cfg_path);
    char *sl = strrchr(cfg_dir, '/');
    if (sl && sl != cfg_dir) { *sl = 0; mkdir_p(cfg_dir); }
    FILE *f = fopen(cfg_path, "wx");
    if (!f) return;
    fputs(default_config, f);
    fclose(f);
    log_info("created default config %s", cfg_path);
}

static void resolve_config_path(const char *cli)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");

    if (cli)
        copy_str(cfg_path, sizeof cfg_path, cli);
    else if (geteuid() == 0 || access(DEFAULT_CONFIG, R_OK) == 0)
        copy_str(cfg_path, sizeof cfg_path, DEFAULT_CONFIG);
    else if (xdg && *xdg)
        snprintf(cfg_path, sizeof cfg_path, "%s/rcomp/config.toml", xdg);
    else if (home && *home)
        snprintf(cfg_path, sizeof cfg_path, "%s/.config/rcomp/config.toml", home);
    else
        copy_str(cfg_path, sizeof cfg_path, DEFAULT_CONFIG);
}

static void setup_watch(void)
{
    copy_str(cfg_dir, sizeof cfg_dir, cfg_path);
    char *sl = strrchr(cfg_dir, '/');
    if (sl) {
        cfg_base = cfg_path + (sl - cfg_dir) + 1;
        if (sl == cfg_dir) cfg_dir[1] = 0; else *sl = 0;
    } else {
        cfg_base = cfg_path;
        copy_str(cfg_dir, sizeof cfg_dir, ".");
    }
    ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (ifd < 0) { log_warn("inotify unavailable, use SIGUSR1 to reload"); return; }
    if (inotify_add_watch(ifd, cfg_dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
        log_warn("cannot watch %s: %s (use SIGUSR1 to reload)", cfg_dir, strerror(errno));
        close(ifd);
        ifd = -1;
    }
}

static void drain_inotify(void)
{
    char ib[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n;
    while ((n = read(ifd, ib, sizeof ib)) > 0) {
        for (char *p = ib; p < ib + n; ) {
            struct inotify_event *e = (struct inotify_event *)p;
            if (e->len && !strcmp(e->name, cfg_base)) reload_requested = 1;
            p += sizeof *e + e->len;
        }
    }
}

static void on_signal(int sig)
{
    char c = (sig == SIGUSR1) ? 'r' : 'q';
    int saved = errno;
    if (write(sigpipe[1], &c, 1) < 0) { /* pipe full: a wakeup is pending anyway */ }
    errno = saved;
}

/* ===================================================================== */
/*  Events                                                               */
/* ===================================================================== */

static void handle_event(XEvent *ev)
{
    Win *w;

    if (ev->type == damage_ev + XDamageNotify) {
        XDamageNotifyEvent *de = (XDamageNotifyEvent *)ev;
        w = find_win(de->drawable);
        if (w) {
            XserverRegion parts = XFixesCreateRegion(dpy, NULL, 0);
            XDamageSubtract(dpy, de->damage, None, parts);
            XFixesTranslateRegion(dpy, parts,
                                  w->a.x + w->a.border_width,
                                  w->a.y + w->a.border_width);
            add_damage(parts);
        } else {
            XDamageSubtract(dpy, de->damage, None, None);
        }
        return;
    }

    switch (ev->type) {
    case CreateNotify:
        if (ev->xcreatewindow.parent == root) add_win(ev->xcreatewindow.window);
        break;

    case DestroyNotify:
        if ((w = find_win(ev->xdestroywindow.window))) {
            w->damage = 0;              /* destroyed together with the window */
            w->destroyed = 1;
            if (!w->fading) remove_win(w);
        }
        break;

    case MapNotify:
        if ((w = find_win(ev->xmap.window))) {
            double start = w->fading ? w->cur : 0.0;
            XGetWindowAttributes(dpy, w->id, &w->a);
            w->mapped = 1;
            w->fading = 0;
            free_pic(w);
            reclassify(w);
            double t = target_opacity(w);
            if (fade_allowed(w) && cfg.fade_in_ms > 0) {
                w->cur = start;
                set_target(w, t, cfg.fade_in_ms);
            } else {
                w->cur = w->tgt = t;
            }
            damage_win(w);
        }
        break;

    case UnmapNotify:
        if ((w = find_win(ev->xunmap.window))) {
            w->mapped = 0;
            if (w->pic && w->cur > 0 && fade_allowed(w) && cfg.fade_out_ms > 0) {
                w->fading = 1;          /* keep the pixmap and fade it out */
                set_target(w, 0.0, cfg.fade_out_ms);
            } else {
                w->fading = 0;
                w->cur = w->tgt = 0;
                free_pic(w);
            }
            damage_win(w);
        }
        break;

    case ReparentNotify:
        if (ev->xreparent.parent == root) {
            add_win(ev->xreparent.window);
        } else if ((w = find_win(ev->xreparent.window))) {
            remove_win(w);
        }
        break;

    case ConfigureNotify: {
        XConfigureEvent *c = &ev->xconfigure;
        if (c->window == root) {
            root_w = c->width;
            root_h = c->height;
            make_buffer();
            damage_all();
            break;
        }
        if ((w = find_win(c->window))) {
            if (visible(w)) damage_win(w);
            if (c->width != w->a.width || c->height != w->a.height ||
                c->border_width != w->a.border_width)
                free_pic(w);            /* old named pixmap is now stale */
            w->a.x = c->x;
            w->a.y = c->y;
            w->a.width = c->width;
            w->a.height = c->height;
            w->a.border_width = c->border_width;
            w->a.override_redirect = c->override_redirect;
            restack(w, c->above);
            if (visible(w)) damage_win(w);
        }
        break;
    }

    case CirculateNotify:
        if ((w = find_win(ev->xcirculate.window))) {
            if (ev->xcirculate.place == PlaceOnTop) {
                unlink_win(w);
                append_win(w);
            } else {
                restack(w, None);
            }
            if (visible(w)) damage_win(w);
        }
        break;

    case PropertyNotify: {
        Atom at = ev->xproperty.atom;
        Window id = ev->xproperty.window;
        if (id == root) {
            if (at == a_active) {
                Window a = read_active();
                if (a == active_win) break;
                active_win = a;
                for (w = wins; w; w = w->next) {
                    /* the WM may set WM_STATE a bit after the frame is mapped */
                    if (w->mapped && !w->client && !w->a.override_redirect)
                        reclassify(w);
                    retarget(w);
                }
            }
            break;
        }
        if (!(w = find_win(id)) && !(w = find_win_by_client(id))) break;
        if (at == a_opacity) {
            w->prop_opacity = read_opacity(id);
            retarget(w);
        } else if (at == a_type || at == XA_WM_CLASS) {
            if (w->mapped) {
                reclassify(w);
                retarget(w);
                damage_win(w);
            }
        }
        break;
    }

    case SelectionClear:
        if (ev->xselectionclear.window == sel_win) {
            log_info("another compositing manager took over, exiting");
            running = 0;
        }
        break;

    case Expose:
        damage_all();
        break;
    }
}

/* ===================================================================== */
/*  main                                                                 */
/* ===================================================================== */

static void usage(FILE *o)
{
    fprintf(o, "usage: rcomp [-c config.toml] [-v] [--dump-config]\n"
               "  -c FILE         config file (default: %s)\n"
               "  -v              verbose logging\n"
               "  --dump-config   print the default config and exit\n"
               "  SIGUSR1         reload the config\n", DEFAULT_CONFIG);
}

int main(int argc, char **argv)
{
    const char *cli_cfg = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) cli_cfg = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--dump-config")) { fputs(default_config, stdout); return 0; }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(stdout); return 0; }
        else { usage(stderr); return 2; }
    }

    /* configuration */
    resolve_config_path(cli_cfg);
    config_defaults(&cfg);
    if (!cli_cfg && access(cfg_path, F_OK) != 0)
        write_default_config();
    if (config_load(cfg_path, &cfg) < 0)
        log_warn("cannot read %s: %s; using built-in defaults", cfg_path, strerror(errno));
    else
        log_info("configuration loaded from %s", cfg_path);
    frame_ms = 1000.0 / cfg.fps;
    setup_watch();

    /* signals via self-pipe */
    if (pipe2(sigpipe, O_NONBLOCK | O_CLOEXEC) < 0) { perror("pipe2"); return 1; }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    /* X connection and extensions */
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "cannot open display\n"); return 1; }
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    XSetErrorHandler(x_error);

    int ev_base, err_base, major, minor;
    if (!XRenderQueryExtension(dpy, &ev_base, &err_base)) {
        fprintf(stderr, "XRender missing\n"); return 1;
    }
    if (!XDamageQueryExtension(dpy, &damage_ev, &damage_err)) {
        fprintf(stderr, "XDamage missing\n"); return 1;
    }
    if (!XFixesQueryExtension(dpy, &ev_base, &err_base)) {
        fprintf(stderr, "XFixes missing\n"); return 1;
    }
    if (!XCompositeQueryExtension(dpy, &ev_base, &err_base)) {
        fprintf(stderr, "XComposite missing\n"); return 1;
    }
    XCompositeQueryVersion(dpy, &major, &minor);
    if (major == 0 && minor < 3) {
        fprintf(stderr, "XComposite >= 0.3 required\n"); return 1;
    }

    /* claim the compositing-manager selection */
    char name[32];
    snprintf(name, sizeof name, "_NET_WM_CM_S%d", scr);
    Atom cm_atom = XInternAtom(dpy, name, False);
    if (XGetSelectionOwner(dpy, cm_atom) != None) {
        fprintf(stderr, "another compositing manager is already running\n");
        return 1;
    }
    sel_win = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XSetSelectionOwner(dpy, cm_atom, sel_win, 0);

    a_opacity  = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    a_active   = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    a_wm_state = XInternAtom(dpy, "WM_STATE", False);
    a_type     = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    for (int t = 1; t < T_COUNT; t++) {
        char an[64];
        size_t k = (size_t)snprintf(an, sizeof an, "_NET_WM_WINDOW_TYPE_");
        for (const char *p = type_names[t]; *p && k + 1 < sizeof an; p++)
            an[k++] = (char)toupper((unsigned char)*p);
        an[k] = 0;
        type_atoms[t] = XInternAtom(dpy, an, False);
    }

    root_w = DisplayWidth(dpy, scr);
    root_h = DisplayHeight(dpy, scr);
    root_fmt = XRenderFindVisualFormat(dpy, DefaultVisual(dpy, scr));

    /* draw on the overlay window; make it transparent to input */
    overlay = XCompositeGetOverlayWindow(dpy, root);
    XserverRegion empty = XFixesCreateRegion(dpy, NULL, 0);
    XFixesSetWindowShapeRegion(dpy, overlay, ShapeBounding, 0, 0, None);
    XFixesSetWindowShapeRegion(dpy, overlay, ShapeInput, 0, 0, empty);
    XFixesDestroyRegion(dpy, empty);

    XRenderPictureAttributes pa;
    pa.subwindow_mode = IncludeInferiors;
    root_pic = XRenderCreatePicture(dpy, overlay, root_fmt, CPSubwindowMode, &pa);
    make_buffer();

    /* start managing */
    active_win = read_active();
    XGrabServer(dpy);
    XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);
    XSelectInput(dpy, root, SubstructureNotifyMask | ExposureMask |
                            StructureNotifyMask | PropertyChangeMask);
    Window r, p, *children;
    unsigned int n;
    if (XQueryTree(dpy, root, &r, &p, &children, &n)) {
        for (unsigned int i = 0; i < n; i++) add_win(children[i]);
        if (children) XFree(children);
    }
    XUngrabServer(dpy);
    damage_all();

    struct pollfd pf[3];
    pf[0].fd = ConnectionNumber(dpy); pf[0].events = POLLIN;
    pf[1].fd = sigpipe[0];            pf[1].events = POLLIN;
    pf[2].fd = ifd;                   pf[2].events = POLLIN;   /* -1 is ignored */

    double next_frame = 0, last_tick = 0;
    int prev_anim = 0;

    while (running) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            handle_event(&ev);
        }
        if (reload_requested) {
            reload_requested = 0;
            reload_config();
        }

        double now = mono_ms();
        int anim = any_animating();
        if ((damage || anim) && now >= next_frame) {
            if (anim) {
                double dt = prev_anim ? now - last_tick : frame_ms;
                if (dt > 100.0) dt = 100.0;
                tick(dt);
                last_tick = now;
            }
            prev_anim = any_animating();
            paint();
            next_frame = now + frame_ms;
        } else if (!anim) {
            prev_anim = 0;
        }

        XFlush(dpy);
        if (XPending(dpy)) continue;

        int timeout = -1;
        if (damage || any_animating()) {
            double t = next_frame - mono_ms();
            timeout = t <= 0 ? 0 : (int)ceil(t);
        }
        pf[0].revents = pf[1].revents = pf[2].revents = 0;
        poll(pf, 3, timeout);

        if (pf[1].revents & POLLIN) {
            char c;
            while (read(sigpipe[0], &c, 1) > 0) {
                if (c == 'r') reload_requested = 1;
                else running = 0;
            }
        }
        if (ifd >= 0 && (pf[2].revents & POLLIN)) drain_inotify();
    }

    /* clean shutdown: give the screen back to the X server */
    XCompositeUnredirectSubwindows(dpy, root, CompositeRedirectManual);
    XCompositeReleaseOverlayWindow(dpy, root);
    XDestroyWindow(dpy, sel_win);
    XCloseDisplay(dpy);
    return 0;
}
