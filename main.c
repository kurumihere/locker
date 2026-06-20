#define _POSIX_C_SOURCE 200809L
#include "backend.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
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
                "usage: %s [-b radius] [-d factor] [-c rrggbb] [-B] [-h]\n"
                "  -b radius   blur radius (default: 3, 0 = off)\n"
                "  -d factor   darken factor 0.0-1.0 (default: 0.3, 0 = off)\n"
                "  -c rrggbb   solid background color instead of screenshot\n"
                "  -B          daemon mode, lock on SIGUSR1\n"
                "  -h          show this help\n",
                name);
}

int
main(int argc, char **argv)
{
        int blur_radius = 3;
        double darken = 0.3;
        const char *bg_color = NULL;
        int daemon = 0;

        int ch;
        while ((ch = getopt(argc, argv, "b:d:c:hB")) != -1) {
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
                        /* parent exits */
                        return 0;
                }
                /* child: become session leader */
                setsid();

                /* close std fds, nobody needs them */
                close(0);
                close(1);
                close(2);
                open("/dev/null", O_RDONLY);  /* stdin */
                open("/dev/null", O_WRONLY);  /* stdout */
                open("/dev/null", O_WRONLY);  /* stderr */

                struct sigaction sa;
                sa.sa_handler = sigusr1_handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;
                sigaction(SIGUSR1, &sa, NULL);

                /* ignore SIGCHLD to avoid zombies */
                sa.sa_handler = SIG_IGN;
                sigaction(SIGCHLD, &sa, NULL);

                for (;;) {
                        if (want_lock && !locked) {
                                locked = 1;
                                want_lock = 0;
                                x11_run(blur_radius, darken, bg_color);
                                locked = 0;
                                want_lock = 0;
                                struct timespec ts = {0, 250000000L};
                                nanosleep(&ts, NULL);
                                continue;
                        }
                        pause();
                }
        }

        return x11_run(blur_radius, darken, bg_color);
}
