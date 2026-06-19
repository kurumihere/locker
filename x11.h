#ifndef LOCKER_X11_H
#define LOCKER_X11_H

#include <X11/Xlib.h>

int x11_init(void);
void x11_cleanup(void);

XImage *x11_capture_screen(void);
XImage *x11_create_solid(unsigned long pixel);
void x11_blur_image(XImage *img, int radius);
void x11_darken_image(XImage *img, double factor);
void x11_destroy_image(XImage *img);

Window x11_create_window(void);
void x11_show_image(Window win, XImage *img);
void x11_hide_cursor(Window win);

int x11_grab_input(Window win);
void x11_ungrab_input(void);
int x11_next_event(XEvent *ev);

void x11_lock_layout(void);
void x11_restore_layout(void);

void x11_draw_indicator(Window win, int count);
void x11_draw_message(Window win, const char *msg);
void x11_clear_area(Window win, int x, int y, int w, int h);

int x11_width(void);
int x11_height(void);

#endif
