/*  Copyright (C) 1996 N.M. Maclaren
    Copyright (C) 1996 The University of Cambridge

This includes all of the 'safe' headers and definitions used across modules.
No changes should be needed for any system that is even remotely like Unix. */



#include "libmsntp.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VERSION         "1.6a"         /* Just the version string */
#define MAX_SOCKETS        10          /* Maximum number of addresses */

#ifndef LOCKNAME
    #define LOCKNAME "/etc/msntp.pid"  /* Stores the pid */
#endif
#ifndef SAVENAME
    #define SAVENAME "/etc/msntp.state" /* Stores the recovery state */
#endif



/* Defined in main.c */

#define op_client           1          /* Behave as a challenge client */
#define op_server           2          /* Behave as a response server */
#define op_listen           3          /* Behave as a listening client */
#define op_broadcast        4          /* Behave as a broadcast server */

extern const char *argv0;

extern int verbose, operation;

extern const char *lockname;

extern void fatal (int errnum, const char *message, const char *insert);



/* Defined in unix.c */

extern void do_nothing (int seconds);

extern int ftty (FILE *file);

extern void set_lock (int lock);

extern void log_message (const char *message);



/* Defined in internet.c */

/* extern void find_address (struct in_addr *address, int *port, char *hostname,
    int timespan); */



/* Defined in socket.c */

extern int open_socket (int which, char *hostnames, int timespan);

extern int write_socket (int which, void *packet, int length);

extern int read_socket (int which, void *packet, int length, int waiting,
                        int *written);

extern int flush_socket (int which, int *count);

extern int msntp_close_socket (int which);



/* Defined in timing.c */

extern double current_time (double offset);

extern time_t convert_time (double value, int *millisecs);

extern int adjust_time (double difference, int immediate, double ignore);

#ifdef __cplusplus
}
#endif