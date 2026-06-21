/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, kurumi
 *
 * See LICENSE for details.
 */

#ifndef LOCKER_PAM_AUTH_H
#define LOCKER_PAM_AUTH_H

#include <stddef.h>

int locker_pam_auth(const char *username, const char *password, char *err_msg,
                    size_t err_msg_size);

#endif
