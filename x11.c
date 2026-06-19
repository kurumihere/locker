#define _POSIX_C_SOURCE 199309L
#include "x11.h"
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

        XGrabPointer(d, win, True,
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, win, None, CurrentTime);

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
