/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, kurumi
 *
 * See LICENSE for details.
 */

#define _POSIX_C_SOURCE 200809L
#include "backend.h"
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t want_lock = 0;
static volatile sig_atomic_t locked = 0;

static void
sigusr1_handler(int sig)
{
        (void)sig;
        want_lock = 1;
}

static void
usage(const char *name)
{
        fprintf(stderr,
                "usage: %s [-b radius] [-d factor] [-c rrggbb] [-i type] [-B] "
                "[-h]\n"
                "  -b radius   blur radius (default: 3, 0 = off)\n"
                "  -d factor   darken factor 0.0-1.0 (default: 0.3, 0 = off, "
                "1.0 = black)\n"
                "  -c rrggbb   solid background color instead of screenshot\n"
                "  -i type     indicator type: 'circle' or 'dots' (default: "
                "circle)\n"
                "  -B          daemon mode, lock on SIGUSR1\n"
                "  -h          show this help\n",
                name);
}

int
main(int argc, char **argv)
{
        setlocale(LC_ALL, "");

        int blur_radius = 3;
        double darken = 0.3;
        const char *bg_color = NULL;
        int daemon = 0;
        IndicatorType ind_type = INDICATOR_CIRCLE;

        int ch;
        while ((ch = getopt(argc, argv, "b:d:c:i:hB")) != -1) {
                switch (ch) {
                case 'b': {
                        char *end;
                        long val = strtol(optarg, &end, 10);
                        if (*end != '\0' || val < 0 || val > 100) {
                                fprintf(stderr, "invalid blur radius: %s\n",
                                        optarg);
                                return 1;
                        }
                        blur_radius = (int)val;
                        break;
                }
                case 'd': {
                        char *end;
                        darken = strtod(optarg, &end);
                        if (*end != '\0' || darken < 0.0 || darken > 1.0) {
                                fprintf(stderr, "invalid darken factor: %s\n",
                                        optarg);
                                return 1;
                        }
                        break;
                }
                case 'c':
                        bg_color = optarg;
                        break;
                case 'i':
                        if (strcmp(optarg, "dots") == 0) {
                                ind_type = INDICATOR_DOTS;
                        } else {
                                ind_type = INDICATOR_CIRCLE;
                        }
                        break;
                case 'B':
                        daemon = 1;
                        break;
                case 'h':
                default:
                        usage(argv[0]);
                        return 1;
                }
        }

        if (daemon) {
                pid_t pid = fork();
                if (pid < 0) {
                        fprintf(stderr, "main: fork failed\n");
                        return 1;
                }
                if (pid > 0) {
                        return 0;
                }
                setsid();

                int devnull = open("/dev/null", O_RDWR);
                if (devnull >= 0) {
                        dup2(devnull, 0);
                        dup2(devnull, 1);
                        dup2(devnull, 2);
                        if (devnull > 2)
                                close(devnull);
                }

                struct sigaction sa;
                sa.sa_handler = sigusr1_handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;
                sigaction(SIGUSR1, &sa, NULL);

                sa.sa_handler = SIG_IGN;
                sigaction(SIGCHLD, &sa, NULL);

                for (;;) {
                        if (want_lock && !locked) {
                                locked = 1;
                                want_lock = 0;
                                x11_run(blur_radius, darken, bg_color,
                                        ind_type);
                                locked = 0;
                                want_lock = 0;
                                struct timespec ts = {0, 250000000L};
                                nanosleep(&ts, NULL);
                                continue;
                        }
                        pause();
                }
        }

        return x11_run(blur_radius, darken, bg_color, ind_type);
}
