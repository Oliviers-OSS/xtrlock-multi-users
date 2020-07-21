/*
 * auth.h
 *
 *  Created on: 12 juil. 2020
 *      Author: oc
 */
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#if (GCC_VERSION > 40000) /* GCC 4.0.0 */
#pragma once
#endif /* GCC 4.0.0 */

#ifndef AUTH_H_
#define AUTH_H_

typedef struct UserAuthenticationData_ {
    char *login;
    char *password;
} UserAuthenticationData;

int auth_shadow(const UserAuthenticationData *userData);
int auth_pam(const UserAuthenticationData *userData);

#ifdef AUTH_USE_PAM
#define authenticate auth_pam
#else
#define authenticate auth_shadow
#endif

#endif /* AUTH_H_ */
