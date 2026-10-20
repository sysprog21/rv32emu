/*
 * Copyright (c) 2026 rv32emu contributors
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error This probe requires Linux x86_64 ucontext REG_RIP
#endif

#define CAPACITY 1048576
static uintptr_t pcs[CAPACITY];
static volatile sig_atomic_t count, dropped, active;
static int output = -1;
static uint64_t begin_cpu;
static pid_t main_thread;
static volatile sig_atomic_t start_error, stop_error, clock_error;
static uint64_t cpu_now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t) != 0) {
        clock_error = errno;
        return 0;
    }
    return (uint64_t) t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static void require_main_thread(void)
{
    if ((pid_t) syscall(SYS_gettid) != main_thread) {
        static const char error[] =
            "CPU sampler requires a single-threaded target\n";
        ssize_t written = write(STDERR_FILENO, error, sizeof(error) - 1);
        (void) written;
        _exit(125);
    }
}

static void sample(int signo, siginfo_t *info, void *context)
{
    (void) signo;
    (void) info;
    if (!active)
        return;
    require_main_thread();
    ucontext_t *u = context;
    if (count < CAPACITY) {
        pcs[count] = (uintptr_t) u->uc_mcontext.gregs[REG_RIP];
        count++;
    } else
        dropped++;
}
static void write_all(const void *ptr, size_t size)
{
    const char *p = ptr;
    while (size) {
        ssize_t n = write(output, p, size);
        if ((n < 0) && (errno == EINTR))
            continue;
        if (n <= 0)
            break;
        p += n;
        size -= (size_t) n;
    }
}
static void control(int signo)
{
    (void) signo;
    int saved_errno = errno;
    require_main_thread();
    struct itimerval timer = {0};
    if (!active) {
        count = dropped = start_error = stop_error = clock_error = 0;
        begin_cpu = cpu_now();
        active = 1;
        timer.it_interval.tv_usec = 1000;
        timer.it_value.tv_usec = 1000;
        if (setitimer(ITIMER_PROF, &timer, NULL) != 0)
            start_error = errno;
    } else {
        active = 0;
        if (setitimer(ITIMER_PROF, &timer, NULL) != 0)
            stop_error = errno;
        uint64_t end_cpu = cpu_now();
        uint64_t header[] = {0x5256333250433032ULL,
                             (uint64_t) count,
                             (uint64_t) dropped,
                             begin_cpu,
                             end_cpu,
                             1000000,
                             (uint64_t) start_error,
                             (uint64_t) stop_error,
                             (uint64_t) clock_error};
        write_all(header, sizeof(header));
        write_all(pcs, (size_t) count * sizeof(pcs[0]));
    }
    errno = saved_errno;
}
static void setup_failure(const char *operation)
{
    int saved_errno = errno;
    dprintf(STDERR_FILENO, "CPU sampler setup failed: %s errno=%d\n", operation,
            saved_errno);
    _exit(125);
}
__attribute__((constructor)) static void install_sampler(void)
{
    const char *path = getenv("RV32_CPU_SAMPLE_OUT");
    if (!path)
        return;
    main_thread = getpid();
    output = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (output < 0)
        setup_failure("open");
    sigset_t blocked;
    if (sigprocmask(SIG_SETMASK, NULL, &blocked) != 0)
        setup_failure("query signal mask");
    if ((sigismember(&blocked, SIGPROF)) || (sigismember(&blocked, SIGUSR2))) {
        errno = EBUSY;
        setup_failure("blocked signal collision");
    }
    struct sigaction previous;
    struct itimerval previous_timer;
    if (sigaction(SIGPROF, NULL, &previous) != 0)
        setup_failure("query SIGPROF");
    if (previous.sa_handler != SIG_DFL) {
        errno = EBUSY;
        setup_failure("SIGPROF collision");
    }
    if (getitimer(ITIMER_PROF, &previous_timer) != 0)
        setup_failure("query ITIMER_PROF");
    if ((previous_timer.it_value.tv_sec) || (previous_timer.it_value.tv_usec) ||
        (previous_timer.it_interval.tv_sec) ||
        (previous_timer.it_interval.tv_usec)) {
        errno = EBUSY;
        setup_failure("ITIMER_PROF collision");
    }
    if (sigaction(SIGUSR2, NULL, &previous) != 0)
        setup_failure("query SIGUSR2");
    if (previous.sa_handler != SIG_DFL) {
        errno = EBUSY;
        setup_failure("SIGUSR2 collision");
    }
    struct sigaction sa = {0};
    sa.sa_sigaction = sample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR2);
    if (sigaction(SIGPROF, &sa, NULL) != 0)
        setup_failure("install SIGPROF");
    sa.sa_handler = control;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGPROF);
    if (sigaction(SIGUSR2, &sa, NULL) != 0)
        setup_failure("install SIGUSR2");
}
