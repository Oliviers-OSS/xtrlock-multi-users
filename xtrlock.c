/*
 * xtrlock.c
 *
 * X Transparent Lock
 *
 * Copyright (C)1993,1994 Ian Jackson
 * 2020 Olivier Charloton Multi-user mode
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xos.h>
#include <linux/limits.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <string.h>
#include <crypt.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <values.h>
#include <syslog.h>

#ifdef SHADOW_PWD
#include <shadow.h>
#endif

#ifdef MULTITOUCH
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#endif

#include "auth.h"
#include "cmdline_parameters.h"
#include "patchlevel.h"
#include "lock.bitmap"
#include "mask.bitmap"
#include "password_icon.xbm"
#include "password_mask.xbm"
#include "user_icon.xbm"
#include "user_mask.xbm"

#ifndef TO_STRING
#define STRING(x) #x
#define TO_STRING(x) STRING(x)
#endif /* STRING */

#define STATE(s,b)  X(s,b)
#define STATE_TABLE \
		STATE(Idle,lock) \
		STATE(LoginName,user) \
		STATE(Password,password)

#define X(s,b)    s,
typedef enum State_ {
    STATE_TABLE
} State;
#undef X

Display *display = NULL;
Window window, root;
cmndline_parameters parameters = {
    .modes = 0x0
};

State nextState(const State state)
{
    State next = Idle;
    switch(state) {
    case Idle:
        next = LoginName;
        break;
    case LoginName:
        next = Password;
        break;
    case Password:
        next = Idle;
        break;
    }
    return next;
}

#define SET_NEW_STORAGE_BUFFER(x)	rbuf = x; \
		bufferSize = sizeof(x); \
		rlen = 0

#define resetState() \
	programState = Idle; \
	SET_NEW_STORAGE_BUFFER(loginName); \
    set_cursor(display, (event_mask)&0,programState);


static inline const char * stateToString(const State state)
{
#define X(s,b) case s: return TO_STRING(s);
    switch(state) {
        STATE_TABLE
    }
#undef X
    return "";
}

#define TIMEOUTPERATTEMPT 30000
#define MAXGOODWILL  (TIMEOUTPERATTEMPT*5)
#define INITIALGOODWILL MAXGOODWILL
#define GOODWILLPORTION 0.3

static char *get_username(char *buffer, const size_t size)
{
    const uid_t uid = getuid();
    const char *username = NULL;

    /* Get the user name from the environment if started as root. */
    if (uid == 0) {
        username = getenv("USER");
    }

    if (username == NULL) {
        struct passwd *pw = getpwuid(uid); /* Get the password entry. */

        if (pw == NULL) {
            syslog(LOG_WARNING,"Passw ord entry not found for user %u",uid);
            return NULL;
        }

        username = pw->pw_name;
    }

    if ((buffer) && (strlen(username) < size)) {
        strcpy(buffer,username);
        return buffer;
    } else {
        return strdup(username);
    }
}

int log_session_access(const char *newuser,Bool success)
{
    int error = EXIT_SUCCESS;
    uid_t uid = getuid();
    char *username = NULL;
    char buffer[100];

    /* Get the user name from the environment if started as root. */
    if (uid == 0)
        username = getenv("USER");

    if (username == NULL) {
        struct passwd *pw;

        /* Get the password entry. */
        pw = getpwuid(uid);

        if (pw != NULL) {
            username = pw->pw_name;
        } else {
            error = ENOENT;
            sprintf(buffer,"(%u)",uid);
            username = buffer;
        }
    }

    if (!newuser) {
        newuser = "???";
    }

    if (success) {
        syslog(LOG_NOTICE,"user %s entering into %s's session",newuser,username);
    } else {
        syslog(LOG_ERR,"user %s authentication has failed to enter into %s's session",newuser,username);
    }

    return error;
}

static inline void log_session_lock()
{
    uid_t uid = getuid();
    char *username = NULL;
    char buffer[16];

    /* Get the user name from the environment if started as root. */
    if (uid == 0)
        username = getenv("USER");

    if (username == NULL) {
        struct passwd *pw;

        /* Get the password entry. */
        pw = getpwuid(uid);

        if (pw != NULL) {
            username = pw->pw_name;
        } else {
            sprintf(buffer,"(%u)",uid);
            username = buffer;
        }
    }

    const char *mode = "mono-user";
    if ((parameters.modes & e_MultiUsers) == e_MultiUsers) {
        mode = "multi-users";
    }
    syslog(LOG_NOTICE,"User %s's session is now locked (%s)",username,mode);
}

static void onExit(void)
{
    if (display) {
        XUngrabKeyboard(display,CurrentTime);
        syslog(LOG_DEBUG,"exit XUngrabKeyboard");
        XFlush(display);
        sleep(1);
        XCloseDisplay(display);
    }
    syslog(LOG_NOTICE,"xtrlock ended");
}

static inline void clear_buffer(char *buffer, size_t size)
{
	const char * const limit = buffer + size;
    for (volatile register char *p = buffer;p < limit;++p) {
    	*p = 0xFD;
    }
}

#if MULTITOUCH
XIEventMask evmask;

/* (Optimistically) attempt to grab multitouch devices which are not
 * intercepted via XGrabPointer. */
void handle_multitouch(Cursor cursor)
{
    XIDeviceInfo *info;
    int xi_ndevices;

    info = XIQueryDevice(display, XIAllDevices, &xi_ndevices);

    int i;
    for (i = 0; i < xi_ndevices; i++) {
        XIDeviceInfo *dev = &info[i];

        int j;
        for (j = 0; j < dev->num_classes; j++) {
            if (dev->classes[j]->type == XITouchClass &&
                    dev->use == XISlavePointer) {
                XIGrabDevice(display, dev->deviceid, window, CurrentTime, cursor,
                             GrabModeAsync, GrabModeAsync, False, &evmask);
            }
        }
    }
    XIFreeDeviceInfo(info);
}
#endif

#define X(s,b) case s: csr_source = XCreateBitmapFromData(display,window,b##_bits,b##_width,b##_height); \
		csr_mask = XCreateBitmapFromData(display,window,b##_mask_bits,b##_mask_width,b##_mask_height); \
		cursor = XCreatePixmapCursor(display,csr_source,csr_mask,&csr_fg,&csr_bg,b##_x_hot,b##_y_hot); \
		syslog(LOG_DEBUG,"New cursor " #b ); \
		break;

void set_cursor(Display *display, unsigned int event_mask,State programState)
{
    XColor csr_fg, csr_bg;
    static Cursor cursor;
    Pixmap csr_source;
    Pixmap csr_mask;

    syslog(LOG_NOTICE,"State = %s",stateToString(programState));

    switch(programState) {
        STATE_TABLE
    }

    int error = XChangeActivePointerGrab(display,event_mask,cursor,CurrentTime);
    if (error != Success) {
        syslog(LOG_ERR,"XChangeActivePointerGrab error %d",error);
    }
    XSync(display,False);
}
#undef X

static inline void printVersion(void)
{
    printf("xtrlock %s" EOL,program_version);
}

static const struct option longopts[] = {
#define NEED_ARG        required_argument
#define NO_ARG          no_argument
#define OPT_ARG         optional_argument
#define X(l,s,t,o)      { TO_STRING(l),o,NULL,TO_STRING(s)[0] },
    CMDLINE_OPTS_TABLE
#undef X
#undef NEED_ARG
#undef NO_ARG
#undef OPT_ARG
    { NULL, 0, NULL, 0 }
};

static inline void printHelp(const char *errorMsg)
{
#define X(l,s,t,o) "-" TO_STRING(s) ", --" TO_STRING(l) t EOL

#define USAGE "Usage: " TO_STRING(PROGNAME) " [OPTIONS]" EOL

    if (errorMsg != NULL) {
        fprintf(stderr, "Error %s" EOL USAGE CMDLINE_OPTS_TABLE, errorMsg);
    } else {
        fprintf(stdout, USAGE CMDLINE_OPTS_TABLE);
    }
#undef X
#undef USAGE
}

static int parse_cmdLine(int argc,char *const argv[])
{
#define NEED_ARG        ":"
#define NO_ARG          ""
#define OPT_ARG         "::"
#define X(l,s,t,o) TO_STRING(s) o

    int error = EXIT_SUCCESS;
    int optc;

    parameters.modes = 0x0;
    while (((optc = getopt_long(argc, argv, CMDLINE_OPTS_TABLE, longopts, NULL)) != -1)
            && (EXIT_SUCCESS == error)) {
        switch (optc) {
        case 'b':
            parameters.modes |= e_Blank;
            break;
        case 'f':
            parameters.modes |= e_ForkAfter;
            break;
        case 'u':
            parameters.modes |= e_MultiUsers;
            break;
        case 'h':
            printHelp(NULL);
            exit(EXIT_SUCCESS);
            break;
        case 'v':
            printVersion();
            exit(EXIT_SUCCESS);
            break;
        case '?':
            error = EINVAL;
            printHelp("");
            break;
        default:
            error = EINVAL;
            printHelp("invalid parameter");
            break;
        } /* switch */
    } /*while(((optc = getopt_long(argc,argv,"cln:phv",longopts,NULL))!= -1) && (EXIT_SUCCESS == error))*/
#undef X
#undef NEED_ARG
#undef NO_ARG
#undef OPT_ARG
    return error;
}

int main(int argc, char **argv)
{
    int error = EXIT_SUCCESS;
    State programState = Idle;
    KeySym ks;
    char cbuf[256];
    int clen, rlen=0;
    long goodwill= INITIALGOODWILL, timeout= 0;
    XSetWindowAttributes attrib;
    Cursor cursor;
    Pixmap csr_source,csr_mask;
    XColor csr_fg, csr_bg, dummy, black;
    int ret, screen;
    UserAuthenticationData user = {NULL,NULL};
    char loginName[LOGIN_NAME_MAX];
    char password[256];
    char *rbuf = loginName;
    size_t bufferSize = sizeof(loginName);
    char *display_name = getenv("DISPLAY");

#ifdef SHADOW_PWD
    struct spwd *sp;
#endif
    struct timeval tv;
    int tvt, gs;
    unsigned int event_mask = KeyPressMask|KeyReleaseMask;

    error = parse_cmdLine(argc,argv);
    if (error != EXIT_SUCCESS) {
        goto loop_x;
    }

    openlog("xtrlock",LOG_CONS|LOG_PID,LOG_AUTH);

    display= XOpenDisplay(0);
    if (display==NULL) {
        fprintf(stderr,"xtrlock (version %s): cannot open display\n",
                program_version);
        exit(1);
    }

    atexit(onExit);

#ifdef MULTITOUCH
    unsigned char mask[XIMaskLen(XI_LASTEVENT)];
    int xi_major = 2, xi_minor = 2, xi_opcode, xi_error, xi_event;

    if (!XQueryExtension(display, INAME, &xi_opcode, &xi_event, &xi_error)) {
        fprintf(stderr, "xtrlock (version %s): No X Input extension\n",
                program_version);
        exit(1);
    }

    if (XIQueryVersion(display, &xi_major, &xi_minor) != Success ||
            xi_major * 10 + xi_minor < 22) {
        fprintf(stderr,"xtrlock (version %s): Need XI 2.2\n",
                program_version);
        exit(1);
    }

    evmask.mask = mask;
    evmask.mask_len = sizeof(mask);
    memset(mask, 0, sizeof(mask));
    evmask.deviceid = XIAllDevices;
    XISetMask(mask, XI_HierarchyChanged);
    XISelectEvents(display, DefaultRootWindow(display), &evmask, 1);
#endif

    attrib.override_redirect= True;

    if ((parameters.modes & e_Blank) == e_Blank) {
        screen = DefaultScreen(display);
        attrib.background_pixel = BlackPixel(display, screen);
        window= XCreateWindow(display,DefaultRootWindow(display),
                              0,0,DisplayWidth(display, screen),DisplayHeight(display, screen),
                              0,DefaultDepth(display, screen), CopyFromParent, DefaultVisual(display, screen),
                              CWOverrideRedirect|CWBackPixel,&attrib);
        XAllocNamedColor(display, DefaultColormap(display, screen), "black", &black, &dummy);
    } else {
        window= XCreateWindow(display,DefaultRootWindow(display),
                              0,0,1,1,0,CopyFromParent,InputOnly,CopyFromParent,
                              CWOverrideRedirect,&attrib);
    }

    XSelectInput(display,window,event_mask);

    csr_source= XCreateBitmapFromData(display,window,lock_bits,lock_width,lock_height);
    csr_mask= XCreateBitmapFromData(display,window,lock_mask_bits,lock_mask_width,lock_mask_height);

    ret = XAllocNamedColor(display,
                           DefaultColormap(display, DefaultScreen(display)),
                           "steelblue3",
                           &dummy, &csr_bg);
    if (ret==0)
        XAllocNamedColor(display,
                         DefaultColormap(display, DefaultScreen(display)),
                         "black",
                         &dummy, &csr_bg);

    ret = XAllocNamedColor(display,
                           DefaultColormap(display,DefaultScreen(display)),
                           "grey25",
                           &dummy, &csr_fg);
    if (ret==0)
        XAllocNamedColor(display,
                         DefaultColormap(display, DefaultScreen(display)),
                         "white",
                         &dummy, &csr_bg);



    cursor= XCreatePixmapCursor(display,csr_source,csr_mask,&csr_fg,&csr_bg,
                                lock_x_hot,lock_y_hot);

    XMapWindow(display,window);
    syslog(LOG_NOTICE,"Window = %lu",window);

    /*Sometimes the WM doesn't ungrab the keyboard quickly enough if
     *launching xtrlock from a keystroke shortcut, meaning xtrlock fails
     *to start We deal with this by waiting (up to 100 times) for 10,000
     *microsecs and trying to grab each time. If we still fail
     *(i.e. after 1s in total), then give up, and emit an error
     */

    gs=0; /*gs==grab successful*/
    for (tvt=0 ; tvt<100; tvt++) {
        ret = XGrabKeyboard(display,window,False,GrabModeAsync,GrabModeAsync,
                            CurrentTime);
        if (ret == GrabSuccess) {
            gs=1;
            break;
        }
        switch(ret) {
        case AlreadyGrabbed:
            fprintf(stderr,"AlreadyGrabbed, retrying...\n");
            break;
        case GrabFrozen:
            fprintf(stderr,"GrabFrozen, retrying...\n");
            break;
        case GrabInvalidTime:
            fprintf(stderr,"GrabInvalidTime, retrying...\n");
            break;
        default:
            fprintf(stderr,"XGrabKeyboard retcode = %d, retrying...\n",ret);
            break;
        }
        /*grab failed; wait .01s*/
        tv.tv_sec=0;
        tv.tv_usec=10000;
        select(1,NULL,NULL,NULL,&tv);
    }
    if (gs==0) {
        fprintf(stderr,"xtrlock (version %s): cannot grab keyboard\n",
                program_version);
        exit(1);
    }

    if (XGrabPointer(display,window,False,(event_mask)&0,
                     GrabModeAsync,GrabModeAsync,None,
                     cursor,CurrentTime)!=GrabSuccess) {
        XUngrabKeyboard(display,CurrentTime);
        fprintf(stderr,"xtrlock (version %s): cannot grab pointer\n",
                program_version);
        exit(1);
    }

    if ((parameters.modes & e_ForkAfter) == e_ForkAfter) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr,"xtrlock (version %s): cannot fork: %s\n",
                    program_version, strerror(errno));
            exit(1);
        } else if (pid > 0) {
            exit(0);
        }
    }

#ifdef MULTITOUCH
    handle_multitouch(cursor);
#endif

    log_session_lock();
    for (;;) {
        XEvent ev;
#ifdef FULL_DEBUG
        syslog(LOG_DEBUG,"waiting events.... ");
#endif
        XNextEvent(display,&ev);
#ifdef FULL_DEBUG
        syslog(LOG_DEBUG,"ev.type = %d",ev.type);
#endif
        switch (ev.type) {
        case KeyPress:
            if (ev.xkey.time < timeout) {
                XBell(display,0);
                break;
            }
            clen= XLookupString(&ev.xkey,cbuf,9,&ks,0);
            switch (ks) {
            case XK_Escape:
            case XK_Clear:
                resetState();
                syslog(LOG_DEBUG,"clear");
                break;
            case XK_Delete:
            case XK_BackSpace:
                if (rlen>0) rlen--;
                if (0 == rlen) {
                    if (LoginName == programState) {
                        resetState();
                    }
                    syslog(LOG_DEBUG,"cleared");
                }
                break;
            case XK_Linefeed:
            case XK_Return:
                if (rlen) {
                    rbuf[rlen] = '\0';
                    if (LoginName == programState) {
                        user.login = rbuf;
#ifdef DUMP_CREDENTIALS
                        syslog(LOG_DEBUG,"Login name = %s",user.login);
#endif
                        SET_NEW_STORAGE_BUFFER(password);
                        programState = nextState(programState);
                        set_cursor(display, (event_mask)&0,programState);
                    } else if (Password == programState) {
                        int error = EXIT_SUCCESS;
                        user.password = rbuf;
#ifdef DUMP_CREDENTIALS
                        syslog(LOG_DEBUG,"password = %s",user.password);
#endif
                        rlen = 0;
                        error = authenticate(&user);
                        clear_buffer(password,sizeof(password));
                        log_session_access(user.login, EXIT_SUCCESS == error);
                        if (EXIT_SUCCESS == error) {
                            goto loop_x;
                        }
                        XBell(display,0);
                        if (timeout) {
                            goodwill+= ev.xkey.time - timeout;
                            if (goodwill > MAXGOODWILL) {
                                goodwill= MAXGOODWILL;
                            }
                        }
                        timeout= -goodwill*GOODWILLPORTION;
                        goodwill+= timeout;
                        timeout+= ev.xkey.time + TIMEOUTPERATTEMPT;
                        resetState();
                    }
                }
                break;
            default:
                if (clen != 1) break;
                if (Idle == programState) {
                    programState = nextState(programState);
                    if ((parameters.modes & e_MultiUsers) != e_MultiUsers) {
                        /* single user mode: auto fill the login name */
                        user.login = get_username(loginName, sizeof(loginName));
                        rbuf = password;
                        rlen = 0;
                        programState = nextState(programState);
                    }

                    set_cursor(display, (event_mask)&0,programState);
                }


                if (rlen < (bufferSize - 1)) { /* allow space for the trailing \0 */
                	rbuf[rlen]=cbuf[0];
                	 rlen++;
                } else {
                	syslog(LOG_ERR,"Storage buffer is too small to store the %s (limit = %zu)",stateToString(programState),bufferSize);
                }

                break;
            }
            break;
#if MULTITOUCH
        case GenericEvent:
            if (ev.xcookie.extension == xi_opcode &&
                    XGetEventData(display,&ev.xcookie) &&
                    ev.xcookie.evtype == XI_HierarchyChanged) {
                handle_multitouch(cursor);
            }
            break;
#endif
        default:
            break;
        }
    }
loop_x:
    closelog();
    return error;
}
