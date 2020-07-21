/*
 * auth.c
 *
 *  Created on: 12 juil. 2020
 *      Author: oc
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <shadow.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <alloca.h>
#include <security/pam_appl.h>

typedef struct UserAuthenticationData_ {
    char *login;
    char *password;
} UserAuthenticationData;

#if 0
int compare(const char *s1, const char *s2)
{
    int n = 0;
    const size_t n1 = strlen(s1);
    const size_t n2 = strlen(s2);
    register const char *c1 = s1;
    register const char *c2 = s2;
    const size_t m = (n1>n2)?n1:n2;
    char *buffer = NULL;
    if (m == n1) {
        register char *c = NULL;
        buffer = (char *)alloca(m);
        strcpy(buffer,s2);
        for(int i = m - s2, c = buffer + n2; i < m; i++) {

        }
    }
}
#endif

int auth_shadow(const UserAuthenticationData *userData)
{
    int error = EXIT_SUCCESS;
    const struct spwd *entry = getspnam(userData->login);
    if (entry) {
        const char *pwdhash = crypt(userData->password, entry->sp_pwdp);
        if (pwdhash) {
            if (strcmp(pwdhash, entry->sp_pwdp) != 0) { //TODO: use a constant time compare function
                error = EINVAL;
                syslog(LOG_ERR,"invalid password for user %s",userData->login);
            }
        } else {
            error = errno;
            syslog(LOG_ERR,"crypt error %d for user %s",error,userData->login);
        }
    } else {
        error = ENOENT;
        syslog(LOG_ERR,"user %s is unknown",userData->login);
    }

    return error;
}

static int pam_conversation(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr)
{
    int pam_status = PAM_SUCCESS;
    if ((num_msg >0 ) && (num_msg < PAM_MAX_NUM_MSG)) {
        struct pam_response *aresp = calloc((size_t) num_msg, sizeof *aresp);
        if (aresp) {
            UserAuthenticationData *userData = (UserAuthenticationData *) appdata_ptr;
            for (register int i = 0; (i < num_msg) && (PAM_SUCCESS == pam_status); i++) {
                switch (msg[i]->msg_style) {
                case PAM_PROMPT_ECHO_ON:
                    aresp[i].resp = strdup(userData->login);
                    break;
                case PAM_PROMPT_ECHO_OFF:
                    aresp[i].resp = strdup(userData->password);
                    break;
                case PAM_TEXT_INFO:
                case PAM_ERROR_MSG:
                    break;
                default:
                    pam_status = PAM_CONV_ERR;
                    break;
                }
            }

            if (PAM_SUCCESS == pam_status) {
                *resp = aresp;
            } else {
                for (register int i = 0; i < num_msg; ++i) {
                    if (aresp[i].resp != NULL) {
                        memset(aresp[i].resp, 0, strlen(aresp[i].resp));
                        free(aresp[i].resp);
                    }
                }
                memset(aresp, 0, num_msg * sizeof *aresp);
                free(aresp);
                *resp = NULL;
            }
        } else {
            pam_status = PAM_BUF_ERR;
        }

    } else {
        pam_status = PAM_CONV_ERR;
    }

    return pam_status;
}

int auth_pam(const UserAuthenticationData *userData)
{
    int error = EXIT_SUCCESS;
    pam_handle_t *pamh = NULL;
    struct pam_conv pamconv = {
        .conv = pam_conversation,
        .appdata_ptr = userData,
    };

    int pam_status = pam_start("xtrlock",userData->login,&pamconv,&pamh);
    if (PAM_SUCCESS == pam_status) {
        pam_status = pam_authenticate(pamh, PAM_SILENT);
        if (PAM_SUCCESS == pam_status) {
            pam_status = pam_acct_mgmt(pamh, PAM_SILENT);
            switch(pam_status) {
            case PAM_SUCCESS:
                break;
            case PAM_USER_UNKNOWN:
            case PAM_ACCT_EXPIRED:
                error = ENOENT;
                syslog(LOG_ERR, "user %s account check error %s",userData->login,pam_strerror(pamh, pam_status));
                break;
            case PAM_NEW_AUTHTOK_REQD:
                do {
                    pam_status = pam_chauthtok(pamh, 0);
                } while(PAM_AUTHTOK_ERR == pam_status);
                if (pam_status != PAM_SUCCESS) {
                    error = EPERM;
                    syslog(LOG_ERR, "user %s password expired then error %s",userData->login,pam_strerror(pamh, pam_status));
                }
            }
        } else {
            syslog(LOG_ERR, "user %s authenticate error %s",userData->login,pam_strerror(pamh, pam_status));
            if (PAM_USER_UNKNOWN == pam_status) {
                error = ENOENT;
            } else {
                error = EINVAL;
            }
        }

        const int pam_end_status = pam_end(pamh, pam_status);
        if (pam_end_status != PAM_SUCCESS) {
            syslog(LOG_ERR, pam_strerror(pamh, pam_end_status));
        }

    } else {
        syslog(LOG_ERR,"pam_start error %d",pam_status);
        error = EAGAIN;
    }
    return error;
}



