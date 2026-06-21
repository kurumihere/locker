/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, kurumi
 *
 * See LICENSE for details.
 */

#ifndef LOCKER_X11_H
#define LOCKER_X11_H

#include "backend.h"
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

int x11_init(void);
void x11_cleanup(void);

XImage *x11_capture_screen(void);
void x11_blur_image(XImage *img, int radius);
void x11_tint_image(XImage *img, double factor, unsigned long tint_rgb);
void x11_destroy_image(XImage *img);

Window x11_create_window(unsigned long bg_pixel);
void x11_redraw(Window win, XImage *img, const char *msg, int pos,
                unsigned long bg_pixel, IndicatorType ind_type);
void x11_hide_cursor(Window win);

int x11_grab_input(Window win);
void x11_ungrab_input(void);
int x11_next_event(XEvent *ev);

void x11_lock_layout(void);
void x11_restore_layout(void);

void x11_draw_indicator(Drawable pm, int count, const char *state,
                        IndicatorType ind_type);
void x11_draw_message(Window win, XftDraw *draw, const char *msg);

#endif
