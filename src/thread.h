/* Pi-hole: A black hole for Internet advertisements
*  (c) 2019 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  Datastructure prototypes
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */
#ifndef THREAD_H
#define THREAD_H

#include "FTL.h"

// Create a thread successfully or log and call exit on failure
pthread_t create_thread_or_fail(void*(*func)(void*), const char* name);

#endif // THREAD_H
