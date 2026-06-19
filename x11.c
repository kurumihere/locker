#define _POSIX_C_SOURCE 199309L
#include "x11.h"
#include "pam_auth.h"
#include "util.h"
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Display *d = NULL;
static int screen;
static Window root;
static GC gc;
static int win_w, win_h;
static XFontStruct *font = NULL;
static int old_kb_group = -1;

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

        font = XLoadQueryFont(d, "fixed");
        if (!font) {
                fprintf(stderr, "x11: cannot load font 'fixed'\n");
                XFreeGC(d, gc);
                XCloseDisplay(d);
                return -1;
        }
        XSetFont(d, gc, font->fid);

        unsigned long white = WhitePixel(d, screen);
        XSetForeground(d, gc, white);

        return 0;
}

void
x11_cleanup(void)
{
        if (!d)
                return;
        if (font)
                XFreeFont(d, font);
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
        if (radius < 1)
                return;

        int w = img->width, h = img->height;
        unsigned long *buf = malloc(w * h * sizeof(unsigned long));
        unsigned long *tmp = malloc(w * h * sizeof(unsigned long));
        if (!buf || !tmp) {
                free(buf);
                free(tmp);
                return;
        }

        for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                        buf[y * w + x] = XGetPixel(img, x, y);

        for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                        int s = x - radius > 0 ? x - radius : 0;
                        int e = x + radius < w - 1 ? x + radius : w - 1;
                        int n = e - s + 1;
                        unsigned long r = 0, g = 0, b = 0;
                        for (int k = s; k <= e; k++) {
                                unsigned long p = buf[y * w + k];
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
                                unsigned long p = tmp[k * w + x];
                                r += (p >> 16) & 0xFF;
                                g += (p >> 8) & 0xFF;
                                b += p & 0xFF;
                        }
                        buf[y * w + x] = (0xFFUL << 24) | ((r / n) << 16) |
                                         ((g / n) << 8) | (b / n);
                }
        }

        for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                        XPutPixel(img, x, y, buf[y * w + x]);

        free(buf);
        free(tmp);
}

void
x11_darken_image(XImage *img, double factor)
{
        for (int y = 0; y < img->height; y++) {
                for (int x = 0; x < img->width; x++) {
                        unsigned long p = XGetPixel(img, x, y);
                        unsigned char r = (p >> 16) & 0xFF;
                        unsigned char g = (p >> 8) & 0xFF;
                        unsigned char b = (p) & 0xFF;
                        p = (0xFFUL << 24) |
                            ((unsigned char)(r * factor) << 16) |
                            ((unsigned char)(g * factor) << 8) |
                            (unsigned char)(b * factor);
                        XPutPixel(img, x, y, p);
                }
        }
}

XImage *
x11_create_solid(unsigned long pixel)
{
        XImage *img =
            XGetImage(d, root, 0, 0, win_w, win_h, AllPlanes, ZPixmap);
        if (!img)
                return NULL;
        for (int y = 0; y < win_h; y++)
                for (int x = 0; x < win_w; x++)
                        XPutPixel(img, x, y, pixel);
        return img;
}

void
x11_destroy_image(XImage *img)
{
        if (img)
                XDestroyImage(img);
}

Window
x11_create_window(void)
{
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.background_pixel = 0;
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
x11_show_image(Window win, XImage *img)
{
        XPutImage(d, win, gc, img, 0, 0, 0, 0, win_w, win_h);
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
        int grabbed = 0;
        for (int i = 0; i < 10; i++) {
                int r = XGrabKeyboard(d, win, True, GrabModeAsync,
                                      GrabModeAsync, CurrentTime);
                if (r == GrabSuccess) {
                        grabbed = 1;
                        break;
                }
                struct timespec ts = {0, 50000000L};
                nanosleep(&ts, NULL);
        }
        if (!grabbed) {
                fprintf(stderr, "x11: cannot grab keyboard\n");
                return -1;
        }

        int pr = XGrabPointer(d, win, True,
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, win, None, CurrentTime);
        if (pr != GrabSuccess)
                fprintf(stderr, "x11: cannot grab pointer\n");

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
x11_draw_indicator(Window win, int count)
{
        int cx = win_w / 2;
        int cy = win_h / 2;
        int char_w = XTextWidth(font, "*", 1);
        int total_w = count * char_w;
        int start_x = cx - total_w / 2;

        for (int i = 0; i < count; i++) {
                XDrawString(d, win, gc, start_x + i * char_w, cy, "*", 1);
        }
}

void
x11_draw_message(Window win, const char *msg)
{
        int w = XTextWidth(font, msg, strlen(msg));
        int x = (win_w - w) / 2;
        int y = win_h / 2 - 20;
        XDrawString(d, win, gc, x, y, msg, strlen(msg));
}

void
x11_clear_area(Window win, int x, int y, int w, int h)
{
        XClearArea(d, win, x, y, w, h, False);
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

int
x11_width(void)
{
        return win_w;
}
int
x11_height(void)
{
        return win_h;
}

int
x11_run(int blur_radius, double darken, const char *bg_color)
{
        (void)blur_radius;
        (void)darken;

        if (x11_init() != 0)
                return 1;

        XImage *img = NULL;

        if (bg_color) {
                unsigned long rgb;
                if (parse_hex(bg_color, &rgb) != 0) {
                        fprintf(stderr, "x11: invalid color '%s'\n", bg_color);
                        x11_cleanup();
                        return 1;
                }
                unsigned char r = (rgb >> 16) & 0xFF;
                unsigned char g = (rgb >> 8) & 0xFF;
                unsigned char b = rgb & 0xFF;
                unsigned long pixel = (0xFFUL << 24) | (r << 16) | (g << 8) | b;
                img = x11_create_solid(pixel);
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

        if (!img) {
                fprintf(stderr, "x11: failed to create image\n");
                x11_cleanup();
                return 1;
        }

        Window win = x11_create_window();
        x11_show_image(win, img);
        x11_hide_cursor(win);
        XFlush(d);

        if (x11_grab_input(win) != 0) {
                x11_destroy_image(img);
                x11_cleanup();
                return 1;
        }

        x11_lock_layout();

        char *username = getenv("USER");
        char password[256];
        int pos = 0;
        password[0] = '\0';

        x11_draw_message(win, "Enter password:");
        x11_draw_indicator(win, 0);

        XEvent ev;
        while (x11_next_event(&ev) == 0) {
                if (ev.type == Expose) {
                        x11_show_image(win, img);
                        x11_draw_message(win, "Enter password:");
                        x11_draw_indicator(win, pos);
                        continue;
                }

                if (ev.type != KeyPress)
                        continue;

                char buf[32] = {0};
                KeySym ks;
                XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);

                if (ks == XK_Return) {
                        if (password[0] == '\0') {
                                x11_show_image(win, img);
                                x11_draw_message(win, "Enter password:");
                                x11_draw_indicator(win, 0);
                                continue;
                        }

                        int ok = locker_pam_auth(username, password);

                        secure_zero(password, sizeof(password));
                        pos = 0;

                        if (ok == 0)
                                break;

                        x11_show_image(win, img);
                        x11_draw_message(win, "Wrong password");
                        x11_draw_indicator(win, 0);

                } else if (ks == XK_Escape) {
                        secure_zero(password, sizeof(password));
                        pos = 0;
                        x11_show_image(win, img);
                        x11_draw_message(win, "Enter password:");
                        x11_draw_indicator(win, 0);

                } else if (ks == XK_BackSpace) {
                        if (pos > 0)
                                password[--pos] = '\0';

                        x11_show_image(win, img);
                        x11_draw_message(win, "Enter password:");
                        x11_draw_indicator(win, pos);

                } else if (buf[0] >= 32 && buf[0] < 127) {
                        if (pos < (int)sizeof(password) - 1) {
                                password[pos++] = buf[0];
                                password[pos] = '\0';
                        }

                        x11_show_image(win, img);
                        x11_draw_message(win, "Enter password:");
                        x11_draw_indicator(win, pos);
                }
        }

        x11_ungrab_input();
        x11_restore_layout();
        x11_destroy_image(img);
        x11_cleanup();
        return 0;
}
