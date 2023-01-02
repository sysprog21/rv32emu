#pragma once

#include <stdbool.h>
#include <time.h>

struct Stopwatch {
    bool running;
    double last_time;
    double total;
};
typedef struct Stopwatch *Stopwatch;

Stopwatch new_Stopwatch(void);
void Stopwtach_reset(Stopwatch Q);
void Stopwatch_delete(Stopwatch S);
void Stopwatch_start(Stopwatch Q);
void Stopwatch_stop(Stopwatch Q);
double Stopwatch_read(Stopwatch Q);
