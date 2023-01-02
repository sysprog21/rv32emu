#include <stdlib.h>

#include "Stopwatch.h"

static double seconds()
{
    return ((double) clock()) / (double) CLOCKS_PER_SEC;
}

void Stopwtach_reset(Stopwatch Q)
{
    Q->running = false;
    Q->last_time = 0.0;
    Q->total = 0.0;
}

Stopwatch new_Stopwatch(void)
{
    Stopwatch S = (Stopwatch) malloc(sizeof(struct Stopwatch));
    if (S == NULL)
        return NULL;

    Stopwtach_reset(S);
    return S;
}

void Stopwatch_delete(Stopwatch S)
{
    if (S != NULL)
        free(S);
}

/* Start resets the timer to 0.0; use resume for continued total */

void Stopwatch_start(Stopwatch Q)
{
    if (!(Q->running)) {
        Q->running = true;
        Q->total = 0.0;
        Q->last_time = seconds();
    }
}

void Stopwatch_stop(Stopwatch Q)
{
    if (Q->running) {
        Q->total += seconds() - Q->last_time;
        Q->running = false;
    }
}

double Stopwatch_read(Stopwatch Q)
{
    if (Q->running) {
        double t = seconds();
        Q->total += t - Q->last_time;
        Q->last_time = t;
    }
    return Q->total;
}
