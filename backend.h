/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, kurumi
 *
 * See LICENSE for details.
 */

#ifndef LOCKER_BACKEND_H
#define LOCKER_BACKEND_H

typedef enum { INDICATOR_CIRCLE, INDICATOR_DOTS } IndicatorType;

int x11_run(int blur_radius, double darken, const char *bg_color,
            IndicatorType ind_type);

#endif
