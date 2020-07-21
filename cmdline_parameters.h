/*
 * cmdline_parameters.h
 *
 *  Created on: 21 juil. 2020
 *      Author: oc
 */
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#if (GCC_VERSION > 40000) /* GCC 4.0.0 */
#pragma once
#endif /* GCC 4.0.0 */

#ifndef _CMDLINE_PARAMETERS_H_
#define _CMDLINE_PARAMETERS_H_

#include <getopt.h>

#define TIMEOUT_NOT_SET	(unsigned int)(-1)

#ifndef PROGNAME
#define PROGNAME "xtrlock"
#endif /* PROGNAME */

#ifndef TO_STRING
#define STRING(x) #x
#define TO_STRING(x) STRING(x)
#endif /* STRING */

#define MODE(m)  X(m)
#define MODE_TABLE \
		MODE(Blank) \
		MODE(ForkAfter) \
		MODE(MultiUsers)

#define X(m)    ev_##m,
typedef enum ModeBitValue_ {
    MODE_TABLE
} ModeBitValue;
#undef X

#define X(m)    e_##m = 1<<ev_##m,
typedef enum ModeValue_ {
    MODE_TABLE
} ModeValue;
#undef X

#define EOL "\n"
#define NLT "\t"

#define O(l,s,t,o)      X(l,s,t,o)

#define CMDLINE_OPTS_TABLE \
                O(blank,b," :blank screen.",NO_ARG) \
                O(fork-after,f," :detach the program from the caller.",NO_ARG) \
                O(multi-user,u," :ask user name before password to allow unlock" EOL NLT "accountability on a generic user session",NO_ARG) \
				O(help,h,": Print this help message and exit.",NO_ARG) \
				O(version,v,": Print the version number of xtrlock and exit.",NO_ARG)

typedef struct cmndline_parameters_ {
    unsigned int modes;
    unsigned int timeout;
} cmndline_parameters;


#endif /* _CMDLINE_PARAMETERS_H_ */
