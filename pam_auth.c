/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, kurumi
 *
 * See LICENSE for details.
 */

#define _POSIX_C_SOURCE 200809L
#include "pam_auth.h"
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct pam_appdata {
        const char *password;
        char *err_msg;
        size_t err_msg_size;
};

static int
pam_conv_cb(int num_msg, const struct pam_message **msg,
            struct pam_response **resp, void *appdata_ptr)
{
        struct pam_appdata *appdata = (struct pam_appdata *)appdata_ptr;

        *resp = calloc(num_msg, sizeof(struct pam_response));
        if (!*resp)
                return PAM_BUF_ERR;

        for (int i = 0; i < num_msg; i++) {
                switch (msg[i]->msg_style) {
                case PAM_PROMPT_ECHO_OFF:
                        if (!appdata->password)
                                goto fail;
                        (*resp)[i].resp = strdup(appdata->password);
                        if (!(*resp)[i].resp)
                                goto fail;
                        break;
                case PAM_PROMPT_ECHO_ON:
                        (*resp)[i].resp = strdup("");
                        if (!(*resp)[i].resp)
                                goto fail;
                        break;
                case PAM_ERROR_MSG:
                case PAM_TEXT_INFO:
                        if (appdata->err_msg && appdata->err_msg_size > 0 &&
                            msg[i]->msg) {
                                snprintf(appdata->err_msg,
                                         appdata->err_msg_size, "%s",
                                         msg[i]->msg);
                                for (size_t j = 0; j < strlen(appdata->err_msg);
                                     j++) {
                                        if (appdata->err_msg[j] == '\n' ||
                                            appdata->err_msg[j] == '\r')
                                                appdata->err_msg[j] = ' ';
                                }
                        }
                        (*resp)[i].resp = strdup("");
                        if (!(*resp)[i].resp)
                                goto fail;
                        break;
                default:
                        goto fail;
                }
        }
        return PAM_SUCCESS;
fail:
        for (int j = 0; j < num_msg; j++)
                free((*resp)[j].resp);
        free(*resp);
        *resp = NULL;
        return PAM_CONV_ERR;
}

int
locker_pam_auth(const char *username, const char *password, char *err_msg,
                size_t err_msg_size)
{
        if (err_msg && err_msg_size > 0)
                err_msg[0] = '\0';

        if (!username || !password)
                return -1;

        static const char *services[] = {"system-auth", "common-auth", "login",
                                         "passwd", NULL};

        struct pam_appdata appdata = {password, err_msg, err_msg_size};
        const struct pam_conv conv = {pam_conv_cb, &appdata};
        pam_handle_t *pamh = NULL;

        for (int i = 0; services[i]; i++) {
                char path[256];
                snprintf(path, sizeof(path), "/etc/pam.d/%s", services[i]);
                if (access(path, F_OK) != 0)
                        continue;

                pamh = NULL;
                if (pam_start(services[i], username, &conv, &pamh) !=
                    PAM_SUCCESS)
                        continue;

                int ret = pam_authenticate(pamh, 0);
                if (ret != PAM_SUCCESS) {
                        pam_end(pamh, ret);
                        return -1;
                }

                ret = pam_acct_mgmt(pamh, 0);
                if (ret != PAM_SUCCESS) {
                        fprintf(stderr, "pam: account check failed: %s\n",
                                pam_strerror(pamh, ret));
                        pam_end(pamh, ret);
                        return -1;
                }

                pam_end(pamh, PAM_SUCCESS);
                return 0;
        }

        fprintf(stderr, "pam: no suitable service found\n");
        return -1;
}
