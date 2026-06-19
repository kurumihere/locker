#define _POSIX_C_SOURCE 200809L
#include "backend.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

        return x11_run(blur_radius, darken, bg_color);
}
