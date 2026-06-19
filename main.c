#define _POSIX_C_SOURCE 200809L
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

static void
usage(const char *name)
{
        fprintf(stderr,
                "usage: %s [-b radius] [-d factor] [-c rrggbb] [-h]\n"
                "  -b radius   blur radius (default: 3, 0 = off)\n"
                "  -d factor   darken factor 0.0-1.0 (default: 0.3, 0 = off)\n"
                "  -c rrggbb   solid background color instead of screenshot\n"
                "  -h          show this help\n",
                name);
}

static int
parse_hex(const char *s, unsigned long *out)
{
        if (!s || !s[0])
                return -1;

        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                s += 2;
                if (!s[0])
                        return -1;
        }

        unsigned long v = 0;
        size_t n = 0;
        for (; s[n]; n++) {
                if (n >= 6)
                        return -1;
                v <<= 4;
                if (s[n] >= '0' && s[n] <= '9')
                        v |= (unsigned long)(s[n] - '0');
                else if (s[n] >= 'a' && s[n] <= 'f')
                        v |= (unsigned long)(s[n] - 'a' + 10);
                else if (s[n] >= 'A' && s[n] <= 'F')
                        v |= (unsigned long)(s[n] - 'A' + 10);
                else
                        return -1;
        }
        if (n == 0)
                return -1;

        *out = v;
        return 0;
}

int
main(int argc, char **argv)
{
        int blur_radius = 3;
        double darken = 0.3;
        const char *bg_color = NULL;

        int ch;
        while ((ch = getopt(argc, argv, "b:d:c:h")) != -1) {
                switch (ch) {
                case 'b':
                        blur_radius = atoi(optarg);
                        break;
                case 'd':
                        darken = atof(optarg);
                        break;
                case 'c':
                        bg_color = optarg;
                        break;
                case 'h':
                default:
                        usage(argv[0]);
                        return 1;
                }
        }

        if (x11_init() != 0)
                return 1;

        XImage *img = NULL;

        if (bg_color) {
                unsigned long rgb;
                if (parse_hex(bg_color, &rgb) != 0) {
                        fprintf(stderr, "main: invalid color '%s'\n", bg_color);
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
                        fprintf(stderr, "main: failed to capture screen\n");
                        x11_cleanup();
                        return 1;
                }
                if (blur_radius > 0)
                        x11_blur_image(img, blur_radius);
                if (darken > 0)
                        x11_darken_image(img, darken);
        }

        if (!img) {
                fprintf(stderr, "main: failed to create image\n");
                x11_cleanup();
                return 1;
        }

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
