/*
 * rcomp.c - minimal X11 compositing manager
 *
 * Features: XComposite redirection, XDamage-driven repaint, XRender
 * compositing onto the composite overlay window, window opacity
 * (_NET_WM_WINDOW_OPACITY), simple drop shadows, ARGB window support.
 *
 * Build:
 *   gcc -O2 -Wall -o rcomp rcomp.c \
 *       -lX11 -lXcomposite -lXdamage -lXfixes -lXrender
 *
 * Run (inside a running X session with a normal window manager):
 *   ./rcomp
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>

#define SHADOW_OFFSET 8
#define SHADOW_ALPHA  0x4000   /* 0..0xffff */

typedef struct Win {
    Window id;
    XWindowAttributes a;
    Damage damage;
    Picture pic;            /* picture of the window's offscreen pixmap */
    double opacity;         /* 0.0 .. 1.0 */
    int viewable;
    int has_alpha;
    XRenderPictFormat *fmt;
    struct Win *next;       /* list is ordered bottom -> top */
} Win;

static Display *dpy;
static int scr;
static Window root, overlay, sel_win;
static Picture root_pic, buf_pic;
static Pixmap buf;
static int root_w, root_h;
static int damage_ev, damage_err;
static Atom opacity_atom;
static Win *wins;
static int dirty = 1;

static int error_handler(Display *d, XErrorEvent *e)
{
    (void)d; (void)e;
    return 0;               /* windows vanish all the time; ignore races */
}

/* ---------------------------------------------------------------- list */

static Win *find_win(Window id)
{
    for (Win *w = wins; w; w = w->next)
        if (w->id == id) return w;
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

/* ------------------------------------------------------------- helpers */

static double get_opacity(Window id)
{
    Atom type; int fmt; unsigned long n, after;
    unsigned char *data = NULL;
    double op = 1.0;

    if (XGetWindowProperty(dpy, id, opacity_atom, 0, 1, False, XA_CARDINAL,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        if (type == XA_CARDINAL && fmt == 32 && n == 1)
            op = (double)(*(unsigned long *)data & 0xffffffffUL) / 4294967295.0;
        XFree(data);
    }
    return op;
}

static void free_pic(Win *w)
{
    if (w->pic) { XRenderFreePicture(dpy, w->pic); w->pic = 0; }
}

static void ensure_pic(Win *w)
{
    if (w->pic || !w->viewable) return;
    Pixmap p = XCompositeNameWindowPixmap(dpy, w->id);
    if (!p) return;
    w->pic = XRenderCreatePicture(dpy, p, w->fmt, 0, NULL);
    XFreePixmap(dpy, p);    /* picture keeps its own reference */
}

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
    w->viewable = (w->a.map_state == IsViewable);
    w->opacity = get_opacity(id);
    w->damage = XDamageCreate(dpy, id, XDamageReportNonEmpty);
    XSelectInput(dpy, id, PropertyChangeMask);
    append_win(w);
    dirty = 1;
}

static void remove_win(Window id)
{
    Win *w = find_win(id);
    if (!w) return;
    unlink_win(w);
    free_pic(w);
    if (w->damage) XDamageDestroy(dpy, w->damage);
    free(w);
    dirty = 1;
}

/* ------------------------------------------------------------- buffers */

static void make_buffer(void)
{
    if (buf_pic) XRenderFreePicture(dpy, buf_pic);
    if (buf) XFreePixmap(dpy, buf);
    buf = XCreatePixmap(dpy, root, root_w, root_h, DefaultDepth(dpy, scr));
    buf_pic = XRenderCreatePicture(dpy, buf,
                  XRenderFindVisualFormat(dpy, DefaultVisual(dpy, scr)),
                  0, NULL);
}

/* --------------------------------------------------------------- paint */

static void paint(void)
{
    XRenderColor bg = { 0x1800, 0x1800, 0x2000, 0xffff };
    XRenderFillRectangle(dpy, PictOpSrc, buf_pic, &bg, 0, 0, root_w, root_h);

    for (Win *w = wins; w; w = w->next) {
        if (!w->viewable) continue;
        ensure_pic(w);
        if (!w->pic) continue;

        int x = w->a.x, y = w->a.y;
        int ww = w->a.width + 2 * w->a.border_width;
        int wh = w->a.height + 2 * w->a.border_width;
        int fullscreen = (ww >= root_w && wh >= root_h);

        /* shadow */
        if (!fullscreen) {
            unsigned short al = (unsigned short)(SHADOW_ALPHA * w->opacity);
            XRenderColor sh = { 0, 0, 0, al };   /* premultiplied black */
            XRenderFillRectangle(dpy, PictOpOver, buf_pic, &sh,
                                 x + SHADOW_OFFSET, y + SHADOW_OFFSET, ww, wh);
        }

        /* window contents */
        if (w->opacity >= 0.999 && !w->has_alpha) {
            XRenderComposite(dpy, PictOpSrc, w->pic, None, buf_pic,
                             0, 0, 0, 0, x, y, ww, wh);
        } else {
            Picture mask = None;
            if (w->opacity < 0.999) {
                XRenderColor c = { 0, 0, 0, (unsigned short)(w->opacity * 0xffff) };
                mask = XRenderCreateSolidFill(dpy, &c);
            }
            XRenderComposite(dpy, PictOpOver, w->pic, mask, buf_pic,
                             0, 0, 0, 0, x, y, ww, wh);
            if (mask) XRenderFreePicture(dpy, mask);
        }
    }

    XRenderComposite(dpy, PictOpSrc, buf_pic, None, root_pic,
                     0, 0, 0, 0, 0, 0, root_w, root_h);
    XFlush(dpy);
    dirty = 0;
}

/* -------------------------------------------------------------- events */

static void handle_event(XEvent *ev)
{
    Win *w;

    if (ev->type == damage_ev + XDamageNotify) {
        XDamageNotifyEvent *de = (XDamageNotifyEvent *)ev;
        XDamageSubtract(dpy, de->damage, None, None);
        dirty = 1;
        return;
    }

    switch (ev->type) {
    case CreateNotify:
        if (ev->xcreatewindow.parent == root) add_win(ev->xcreatewindow.window);
        break;

    case DestroyNotify:
        remove_win(ev->xdestroywindow.window);
        break;

    case MapNotify:
        if ((w = find_win(ev->xmap.window))) {
            XGetWindowAttributes(dpy, w->id, &w->a);
            w->viewable = 1;
            free_pic(w);
            dirty = 1;
        }
        break;

    case UnmapNotify:
        if ((w = find_win(ev->xunmap.window))) {
            w->viewable = 0;
            free_pic(w);
            dirty = 1;
        }
        break;

    case ReparentNotify:
        if (ev->xreparent.parent == root) add_win(ev->xreparent.window);
        else remove_win(ev->xreparent.window);
        break;

    case ConfigureNotify: {
        XConfigureEvent *c = &ev->xconfigure;
        if (c->window == root) {
            root_w = c->width;
            root_h = c->height;
            make_buffer();
            dirty = 1;
            break;
        }
        if ((w = find_win(c->window))) {
            if (c->width != w->a.width || c->height != w->a.height ||
                c->border_width != w->a.border_width)
                free_pic(w);    /* old named pixmap is now stale */
            w->a.x = c->x;
            w->a.y = c->y;
            w->a.width = c->width;
            w->a.height = c->height;
            w->a.border_width = c->border_width;
            restack(w, c->above);
            dirty = 1;
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
            dirty = 1;
        }
        break;

    case PropertyNotify:
        if (ev->xproperty.atom == opacity_atom &&
            (w = find_win(ev->xproperty.window))) {
            w->opacity = get_opacity(w->id);
            dirty = 1;
        }
        break;

    case Expose:
        dirty = 1;
        break;
    }
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "cannot open display\n"); return 1; }
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    XSetErrorHandler(error_handler);

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

    opacity_atom = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    root_w = DisplayWidth(dpy, scr);
    root_h = DisplayHeight(dpy, scr);

    /* draw on the overlay window; make it transparent to input */
    overlay = XCompositeGetOverlayWindow(dpy, root);
    XserverRegion empty = XFixesCreateRegion(dpy, NULL, 0);
    XFixesSetWindowShapeRegion(dpy, overlay, ShapeBounding, 0, 0, None);
    XFixesSetWindowShapeRegion(dpy, overlay, ShapeInput, 0, 0, empty);
    XFixesDestroyRegion(dpy, empty);

    XRenderPictureAttributes pa;
    pa.subwindow_mode = IncludeInferiors;
    root_pic = XRenderCreatePicture(dpy, overlay,
                   XRenderFindVisualFormat(dpy, DefaultVisual(dpy, scr)),
                   CPSubwindowMode, &pa);
    make_buffer();

    /* start managing */
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

    for (;;) {
        while (XPending(dpy) || !dirty) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            handle_event(&ev);
        }
        paint();
    }
    return 0;
}
