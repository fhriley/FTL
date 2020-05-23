#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include "thread.h"
#include "log.h"

pthread_t create_thread_or_fail(void*(*func)(void*), const char* name)
{
	// We will use the attributes object later to start all threads in
	// detached mode
	pthread_attr_t attr;
	// Initialize thread attributes object with default attribute values
	pthread_attr_init(&attr);
	// When a detached thread terminates, its resources are automatically
	// released back to the system without the need for another thread to
	// join with the terminated thread
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pthread_t thread;

	if (pthread_create(&thread, &attr, func, NULL) != 0)
	{
		logg("Unable to create %s thread. Exiting...", name);
		exit(EXIT_FAILURE);
	}

	pthread_setname_np(thread, name);

	return thread;
}
