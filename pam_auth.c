#define _POSIX_C_SOURCE 200809L
#include "pam_auth.h"
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
pam_conv_cb(int num_msg, const struct pam_message **msg,
            struct pam_response **resp, void *appdata)
{
        const char *password = (const char *)appdata;

        *resp = calloc(num_msg, sizeof(struct pam_response));
        if (!*resp)
                return PAM_BUF_ERR;

        for (int i = 0; i < num_msg; i++) {
                switch (msg[i]->msg_style) {
                case PAM_PROMPT_ECHO_OFF:
                        if (!password)
                                goto fail;
                        (*resp)[i].resp = strdup(password);
                        if (!(*resp)[i].resp)
                                goto fail;
                        break;
                case PAM_PROMPT_ECHO_ON:
                case PAM_ERROR_MSG:
                case PAM_TEXT_INFO:
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
locker_pam_auth(const char *username, const char *password)
{
        if (!username || !password)
                return -1;

        static const char *services[] = {"login", "system-auth", "common-auth",
                                         "passwd", NULL};

        const struct pam_conv conv = {pam_conv_cb, (void *)password};
        pam_handle_t *pamh = NULL;

        for (int i = 0; services[i]; i++) {
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
