#include "pam_auth.h"
#include "x11.h"
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
secure_zero(void *s, size_t n)
{
        volatile unsigned char *p = s;
        while (n--)
                *p++ = 0;
}

int
main(void)
{
        if (x11_init() != 0)
                return 1;

        XImage *img = x11_capture_screen();
        if (!img) {
                fprintf(stderr, "main: failed to capture screen\n");
                x11_cleanup();
                return 1;
        }

        x11_darken_image(img, 0.3);

        Window win = x11_create_window();
        x11_show_image(win, img);
        x11_hide_cursor(win);

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

                        if (ok == 0) {
                                break;
                        }

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
                        if (pos > 0) {
                                password[--pos] = '\0';
                        }

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
