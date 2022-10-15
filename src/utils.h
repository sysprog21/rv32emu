#pragma once

#include <sys/time.h>

/* Obtain the system 's notion of the current Greenwich time.
 * TODO: manipulate current time zone.
 */
void rv_gettimeofday(struct timeval *tv);