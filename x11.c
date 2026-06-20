/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, kurumi
 *
 * See LICENSE for details.
 */

#define _POSIX_C_SOURCE 200809L
#include "x11.h"
#include "pam_auth.h"
#include "util.h"
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>
#include <X11/keysym.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

static Display *d = NULL;
static int screen;
static Window root;
static GC gc;
static int win_w, win_h;
static XftFont *font = NULL;
static XftColor color;
static int old_kb_group = -1;

static unsigned long color_bg;
static unsigned long color_ring_idle;
static unsigned long color_ring_active;
static unsigned long color_ring_err;
static unsigned long color_ring_auth;

static unsigned long
x11_get_color(const char *hex)
{
        XColor xc;
        Colormap cmap = DefaultColormap(d, screen);
        if (XParseColor(d, cmap, hex, &xc) && XAllocColor(d, cmap, &xc)) {
                return xc.pixel;
        }
        return BlackPixel(d, screen);
}

int
x11_init(void)
{
        d = XOpenDisplay(NULL);
        if (!d) {
                fprintf(stderr, "x11: cannot open display\n");
                return -1;
        }

        screen = DefaultScreen(d);
        root = RootWindow(d, screen);

        win_w = DisplayWidth(d, screen);
        win_h = DisplayHeight(d, screen);

        gc = XCreateGC(d, root, 0, NULL);
        if (!gc) {
                fprintf(stderr, "x11: cannot create GC\n");
                XCloseDisplay(d);
                return -1;
        }

        font = XftFontOpenName(d, screen, "sans-serif:size=18");
        if (!font) {
                font = XftFontOpenName(d, screen, "fixed");
                if (!font) {
                        fprintf(stderr, "x11: cannot load font\n");
                        XFreeGC(d, gc);
                        XCloseDisplay(d);
                        return -1;
                }
        }
        XRenderColor xrcolor = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
        XftColorAllocValue(d, DefaultVisual(d, screen),
                           DefaultColormap(d, screen), &xrcolor, &color);

        unsigned long white = WhitePixel(d, screen);
        XSetForeground(d, gc, white);

        color_bg = x11_get_color("#1a1a1a");
        color_ring_idle = x11_get_color("#444444");
        color_ring_active = x11_get_color("#00d2ff");
        color_ring_err = x11_get_color("#ef4444");
        color_ring_auth = x11_get_color("#3b82f6");

        return 0;
}

void
x11_cleanup(void)
{
        if (!d)
                return;
        if (font) {
                XftColorFree(d, DefaultVisual(d, screen),
                             DefaultColormap(d, screen), &color);
                XftFontClose(d, font);
        }
        if (gc)
                XFreeGC(d, gc);
        XCloseDisplay(d);
}

XImage *
x11_capture_screen(void)
{
        return XGetImage(d, root, 0, 0, win_w, win_h, AllPlanes, ZPixmap);
}

void
x11_blur_image(XImage *img, int radius)
{
        if (radius < 1 || img->bits_per_pixel != 32)
                return;

        int w = img->width, h = img->height;
        uint32_t *data = (uint32_t *)img->data;
        uint32_t *tmp = malloc(w * h * sizeof(uint32_t));
        if (!tmp)
                return;

        for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                        int s = x - radius > 0 ? x - radius : 0;
                        int e = x + radius < w - 1 ? x + radius : w - 1;
                        int n = e - s + 1;
                        unsigned long r = 0, g = 0, b = 0;
                        for (int k = s; k <= e; k++) {
                                uint32_t p = data[y * w + k];
                                r += (p >> 16) & 0xFF;
                                g += (p >> 8) & 0xFF;
                                b += p & 0xFF;
                        }
                        tmp[y * w + x] = (0xFFUL << 24) | ((r / n) << 16) |
                                         ((g / n) << 8) | (b / n);
                }
        }

        for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                        int s = y - radius > 0 ? y - radius : 0;
                        int e = y + radius < h - 1 ? y + radius : h - 1;
                        int n = e - s + 1;
                        unsigned long r = 0, g = 0, b = 0;
                        for (int k = s; k <= e; k++) {
                                uint32_t p = tmp[k * w + x];
                                r += (p >> 16) & 0xFF;
                                g += (p >> 8) & 0xFF;
                                b += p & 0xFF;
                        }
                        data[y * w + x] = (0xFFUL << 24) | ((r / n) << 16) |
                                          ((g / n) << 8) | (b / n);
                }
        }

        free(tmp);
}

void
x11_darken_image(XImage *img, double factor)
{
        if (img->bits_per_pixel != 32)
                return;
        uint32_t *data = (uint32_t *)img->data;
        for (int y = 0; y < img->height; y++) {
                for (int x = 0; x < img->width; x++) {
                        uint32_t p = data[y * img->width + x];
                        unsigned char r = (p >> 16) & 0xFF;
                        unsigned char g = (p >> 8) & 0xFF;
                        unsigned char b = (p) & 0xFF;
                        p = (0xFFUL << 24) |
                            ((unsigned char)(r * factor) << 16) |
                            ((unsigned char)(g * factor) << 8) |
                            (unsigned char)(b * factor);
                        data[y * img->width + x] = p;
                }
        }
}

void
x11_destroy_image(XImage *img)
{
        if (img)
                XDestroyImage(img);
}

Window
x11_create_window(unsigned long bg_pixel)
{
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.background_pixel = bg_pixel;
        attrs.event_mask = KeyPressMask | ExposureMask;

        Window win = XCreateWindow(
            d, root, 0, 0, win_w, win_h, 0, CopyFromParent, InputOutput,
            CopyFromParent, CWOverrideRedirect | CWBackPixel | CWEventMask,
            &attrs);

        XMapWindow(d, win);
        XRaiseWindow(d, win);

        return win;
}

void
x11_redraw(Window win, XImage *img, const char *msg, int pos, unsigned long bg_pixel, IndicatorType ind_type)
{
        Pixmap pm = XCreatePixmap(d, win, win_w, win_h, DefaultDepth(d, screen));
        if (!pm)
                return;

        if (img) {
                XPutImage(d, pm, gc, img, 0, 0, 0, 0, win_w, win_h);
        } else {
                XSetForeground(d, gc, bg_pixel);
                XFillRectangle(d, pm, gc, 0, 0, win_w, win_h);
        }

        XftDraw *pm_draw = XftDrawCreate(d, pm, DefaultVisual(d, screen),
                                         DefaultColormap(d, screen));
        if (pm_draw) {
                if (msg)
                        x11_draw_message(win, pm_draw, msg);
                XftDrawDestroy(pm_draw);
        }

        const char *state = "normal";
        if (msg) {
                if (strcmp(msg, "Wrong password") == 0)
                        state = "wrong";
                else if (strcmp(msg, "Authenticating...") == 0)
                        state = "checking";
        }
        x11_draw_indicator(pm, pos, state, ind_type);

        XCopyArea(d, pm, win, gc, 0, 0, win_w, win_h, 0, 0);
        XFreePixmap(d, pm);
}

void
x11_hide_cursor(Window win)
{
        Pixmap bm = XCreateBitmapFromData(d, win, "\0\0\0\0\0\0\0\0", 1, 1);
        XColor dummy = {0};
        Cursor cursor = XCreatePixmapCursor(d, bm, bm, &dummy, &dummy, 0, 0);
        XDefineCursor(d, win, cursor);
        XFreePixmap(d, bm);
        XFreeCursor(d, cursor);
}

int
x11_grab_input(Window win)
{
        while (1) {
                if (XGrabKeyboard(d, win, True, GrabModeAsync, GrabModeAsync,
                                  CurrentTime) == GrabSuccess)
                        break;
                struct timespec ts = {0, 50000000L};
                nanosleep(&ts, NULL);
        }

        while (XGrabPointer(d, win, True,
                            ButtonPressMask | ButtonReleaseMask |
                                PointerMotionMask,
                            GrabModeAsync, GrabModeAsync, win, None,
                            CurrentTime) != GrabSuccess) {
                struct timespec ts = {0, 50000000L};
                nanosleep(&ts, NULL);
        }

        return 0;
}

void
x11_ungrab_input(void)
{
        XUngrabKeyboard(d, CurrentTime);
        XUngrabPointer(d, CurrentTime);
}

int
x11_next_event(XEvent *ev)
{
        return XNextEvent(d, ev);
}

void
x11_draw_indicator(Drawable pm, int count, const char *state, IndicatorType ind_type)
{
        int cx = win_w / 2;
        int cy = win_h / 2 + 40;

        if (ind_type == INDICATOR_DOTS) {
                int r = 6;
                int spacing = 20;

                if (strcmp(state, "wrong") == 0) {
                        int n = count;
                        int start_x = cx - ((n - 1) * spacing) / 2;
                        XSetForeground(d, gc, color_ring_err);
                        for (int i = 0; i < n; i++) {
                                XFillArc(d, pm, gc, start_x + i * spacing - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);
                        }
                } else if (strcmp(state, "checking") == 0) {
                        int n = count;
                        int start_x = cx - ((n - 1) * spacing) / 2;
                        XSetForeground(d, gc, color_ring_auth);
                        for (int i = 0; i < n; i++) {
                                XFillArc(d, pm, gc, start_x + i * spacing - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);
                        }
                } else if (count == 0) {
                        int n = 5;
                        int start_x = cx - ((n - 1) * spacing) / 2;
                        XSetLineAttributes(d, gc, 2, LineSolid, CapRound, JoinRound);
                        XSetForeground(d, gc, color_ring_idle);
                        for (int i = 0; i < n; i++) {
                                XDrawArc(d, pm, gc, start_x + i * spacing - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);
                        }
                        XSetLineAttributes(d, gc, 1, LineSolid, CapRound, JoinRound);
                } else {
                        int start_x = cx - ((count - 1) * spacing) / 2;
                        XSetForeground(d, gc, color_ring_active);
                        for (int i = 0; i < count; i++) {
                                XFillArc(d, pm, gc, start_x + i * spacing - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);
                        }
                }
        } else {
                int r = 30;

                XSetForeground(d, gc, color_bg);
                XFillArc(d, pm, gc, cx - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);

                XSetLineAttributes(d, gc, 3, LineSolid, CapRound, JoinRound);
                XSetForeground(d, gc, color_ring_idle);
                XDrawArc(d, pm, gc, cx - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);

                if (strcmp(state, "wrong") == 0) {
                        XSetForeground(d, gc, color_ring_err);
                        XDrawArc(d, pm, gc, cx - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);
                } else if (strcmp(state, "checking") == 0) {
                        XSetForeground(d, gc, color_ring_auth);
                        XDrawArc(d, pm, gc, cx - r, cy - r, 2 * r, 2 * r, 45 * 64, 270 * 64);
                } else if (count > 0) {
                        XSetForeground(d, gc, color_ring_active);
                        int start_angle = 90 * 64;
                        int travel = -(count * 30) * 64;
                        XDrawArc(d, pm, gc, cx - r, cy - r, 2 * r, 2 * r, start_angle, travel);
                        XFillArc(d, pm, gc, cx - 4, cy - 4, 8, 8, 0, 360 * 64);
                }

                XSetLineAttributes(d, gc, 1, LineSolid, CapRound, JoinRound);
        }
}

void
x11_draw_message(Window win, XftDraw *draw, const char *msg)
{
        (void)win;
        XGlyphInfo extents;
        XftTextExtentsUtf8(d, font, (const FcChar8 *)msg, strlen(msg),
                           &extents);
        int w = extents.xOff;
        int x = (win_w - w) / 2;
        int y = win_h / 2 - 20;
        XftDrawStringUtf8(draw, &color, font, x, y, (const FcChar8 *)msg,
                          strlen(msg));
}

void
x11_lock_layout(void)
{
        XkbStateRec state;
        if (XkbGetState(d, XkbUseCoreKbd, &state) == Success) {
                old_kb_group = state.group;
                XkbLockGroup(d, XkbUseCoreKbd, 0);
        }
}

void
x11_restore_layout(void)
{
        if (old_kb_group >= 0)
                XkbLockGroup(d, XkbUseCoreKbd, old_kb_group);
        old_kb_group = -1;
}

struct auth_task {
	char username[256];
	char password[256];
	int write_fd;
};

static void *
auth_thread_func(void *arg)
{
	struct auth_task *task = arg;
	int ok = locker_pam_auth(task->username, task->password);
	secure_zero(task->password, sizeof(task->password));
	ssize_t written = write(task->write_fd, &ok, sizeof(ok));
	(void)written;
	free(task);
	return NULL;
}

int
x11_run(int blur_radius, double darken, const char *bg_color, IndicatorType ind_type)
{
        if (x11_init() != 0)
                return 1;

        XImage *img = NULL;
        unsigned long bg_pixel = 0;

        if (bg_color) {
                unsigned long rgb;
                if (parse_hex(bg_color, &rgb) != 0) {
                        fprintf(stderr, "x11: invalid color '%s'\n", bg_color);
                        x11_cleanup();
                        return 1;
                }
                bg_pixel = rgb;
        } else {
                img = x11_capture_screen();
                if (!img) {
                        fprintf(stderr, "x11: failed to capture screen\n");
                        x11_cleanup();
                        return 1;
                }
                if (blur_radius > 0)
                        x11_blur_image(img, blur_radius);
                if (darken > 0)
                        x11_darken_image(img, darken);
        }

        Window win = x11_create_window(bg_pixel);
        x11_redraw(win, img, NULL, -1, bg_pixel, ind_type);
        x11_hide_cursor(win);
        XFlush(d);

        x11_grab_input(win);

        x11_lock_layout();
        XScreenSaverSuspend(d, True);
        XFlush(d);

        char *username = getenv("USER");
        if (!username) {
                fprintf(stderr, "x11: $USER not set\n");
                x11_ungrab_input();
                x11_restore_layout();
                x11_destroy_image(img);
                x11_cleanup();
                return 1;
        }

        char password[256];
        int pos = 0;
        password[0] = '\0';

        int auth_pipe[2];
        if (pipe(auth_pipe) != 0) {
                fprintf(stderr, "x11: failed to create auth pipe\n");
                x11_ungrab_input();
                x11_restore_layout();
                x11_destroy_image(img);
                x11_cleanup();
                return 1;
        }
        int auth_in_progress = 0;
        int auth_dots_count = 0;

        x11_redraw(win, img, "Enter password:", 0, bg_pixel, ind_type);

        int has_dpms = 0;
        int dummy1, dummy2;
        if (DPMSQueryExtension(d, &dummy1, &dummy2)) {
                has_dpms = 1;
        }

        int xfd = ConnectionNumber(d);
        int screen_off = 0;

        for (;;) {
                if (XPending(d) == 0) {
                        fd_set fds;
                        FD_ZERO(&fds);
                        FD_SET(xfd, &fds);
                        FD_SET(auth_pipe[0], &fds);
                        int max_fd = xfd > auth_pipe[0] ? xfd : auth_pipe[0];
                        struct timeval tv = {10, 0};

                        int r = select(max_fd + 1, &fds, NULL, NULL, &tv);
                        if (r == 0 && has_dpms && !screen_off) {
                                DPMSForceLevel(d, DPMSModeOff);
                                XSync(d, False);
                                screen_off = 1;
                        }

                        if (r > 0 && FD_ISSET(auth_pipe[0], &fds)) {
                                int auth_result = -1;
                                if (read(auth_pipe[0], &auth_result, sizeof(auth_result)) == sizeof(auth_result)) {
                                        auth_in_progress = 0;
                                        if (auth_result == 0) {
                                                auth_dots_count = 0;
                                                break;
                                        }
                                        x11_redraw(win, img, "Wrong password", auth_dots_count, bg_pixel, ind_type);
                                        auth_dots_count = 0;
                                }
                        }

                        if (XPending(d) == 0)
                                continue;
                }

                XEvent ev;
                x11_next_event(&ev);

                if (screen_off && has_dpms) {
                        DPMSForceLevel(d, DPMSModeOn);
                        XSync(d, False);
                        screen_off = 0;
                }
                if (ev.type == Expose) {
                        if (auth_in_progress) {
                                x11_redraw(win, img, "Authenticating...", auth_dots_count, bg_pixel, ind_type);
                        } else {
                                x11_redraw(win, img, "Enter password:", pos, bg_pixel, ind_type);
                        }
                        continue;
                }

                if (ev.type != KeyPress)
                        continue;

                if (auth_in_progress)
                        continue;

                char buf[32] = {0};
                KeySym ks;
                int len = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);

                if (ks == XK_Return) {
                        if (password[0] == '\0') {
                                x11_redraw(win, img, "Enter password:", 0, bg_pixel, ind_type);
                                continue;
                        }

                        auth_dots_count = pos;
                        struct auth_task *task = malloc(sizeof(struct auth_task));
                        if (task) {
                                snprintf(task->username, sizeof(task->username), "%s", username);
                                snprintf(task->password, sizeof(task->password), "%s", password);
                                task->write_fd = auth_pipe[1];

                                auth_in_progress = 1;
                                x11_redraw(win, img, "Authenticating...", auth_dots_count, bg_pixel, ind_type);
                                XFlush(d);

                                pthread_t thread;
                                if (pthread_create(&thread, NULL, auth_thread_func, task) == 0) {
                                        pthread_detach(thread);
                                } else {
                                        int ok = locker_pam_auth(username, password);
                                        auth_in_progress = 0;
                                        if (ok == 0) {
                                                auth_dots_count = 0;
                                                break;
                                        }
                                        x11_redraw(win, img, "Wrong password", auth_dots_count, bg_pixel, ind_type);
                                        auth_dots_count = 0;
                                        free(task);
                                }
                        } else {
                                int ok = locker_pam_auth(username, password);
                                if (ok == 0)
                                        break;
                                x11_redraw(win, img, "Wrong password", pos, bg_pixel, ind_type);
                        }

                        secure_zero(password, sizeof(password));
                        pos = 0;

                } else if (ks == XK_Escape) {
                        secure_zero(password, sizeof(password));
                        pos = 0;
                        x11_redraw(win, img, "Enter password:", 0, bg_pixel, ind_type);

                } else if (ks == XK_BackSpace) {
                        if (pos > 0)
                                password[--pos] = '\0';

                        x11_redraw(win, img, "Enter password:", pos, bg_pixel, ind_type);

                } else if (len > 0) {
                        int added = 0;
                        for (int i = 0; i < len; i++) {
                                unsigned char c = buf[i];
                                if (c < 32 || c == 127)
                                        continue;
                                if (pos < (int)sizeof(password) - 1) {
                                        password[pos++] = c;
                                        password[pos] = '\0';
                                        added = 1;
                                }
                        }

                        if (added) {
                                x11_redraw(win, img, "Enter password:", pos, bg_pixel, ind_type);
                        }
                }
        }

        close(auth_pipe[0]);
        close(auth_pipe[1]);

        x11_ungrab_input();
        x11_restore_layout();
        XScreenSaverSuspend(d, False);
        x11_destroy_image(img);
        x11_cleanup();
        return 0;
}
