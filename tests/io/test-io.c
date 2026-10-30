/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Regression tests for guest memory bounds checking.
 *
 * A guest address is a 32 bit number chosen by the guest, so every bulk
 * accessor has to prove the range lies inside the arena before touching it.
 * memory_read() once did not, which let a guest read host memory past the end
 * of the arena and hand it back through write(2).
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "io.h"

#if HAVE_MMAP
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define ARENA_SIZE (1 * 1024 * 1024)
#define PATTERN 0xA5

/* Report whether every byte of the buffer was cleared, which is what a refused
 * read must leave behind: never stale host bytes.
 */
static bool all_zero(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (buf[i])
            return false;
    }
    return true;
}

#if HAVE_MMAP
static void previous_handler(int sig)
{
    _exit(sig == SIGBUS ? 0 : 1);
}

static void test_active_chunk_fault(bool use_default)
{
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        /* A repeated fault must reach the handler installed before the arena.
         * The alarm makes an accidental endless retry fail deterministically.
         */
        alarm(5);
        struct sigaction sa = {0};
        sa.sa_handler = use_default ? SIG_DFL : previous_handler;
        sa.sa_flags = use_default ? SA_SIGINFO : 0;
        sigemptyset(&sa.sa_mask);
        assert(!sigaction(SIGBUS, &sa, NULL));
        memory_t *mem = memory_new(ARENA_SIZE);
        assert(mem);
        mem->mem_base[0] = 1;
        siginfo_t info = {0};
        info.si_addr = mem->mem_base;
        struct sigaction paging;
        assert(!sigaction(SIGBUS, NULL, &paging));
        /* The first fault on an active chunk may be a lost race with another
         * thread and is retried; the repeat is real and must be chained.
         */
        paging.sa_sigaction(SIGBUS, &info, NULL);
        paging.sa_sigaction(SIGBUS, &info, NULL);
        _exit(1);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    if (use_default)
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGBUS);
    else
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
#endif

int main(void)
{
    memory_t *mem = memory_new(ARENA_SIZE);
    assert(mem);
    assert(mem->mem_size == ARENA_SIZE);
    assert(!memory_new(ARENA_SIZE));

    errno = EDOM;
    *(volatile uint8_t *) mem->mem_base = 0;
    assert(errno == EDOM);

    uint8_t src[64];
    memset(src, PATTERN, sizeof(src));
    assert(memory_write(mem, 0, src, sizeof(src)));

    uint8_t dst[64];

    /* an in-range read still returns the data */
    memset(dst, 0, sizeof(dst));
    assert(memory_read(mem, dst, 0, sizeof(dst)));
    assert(!memcmp(dst, src, sizeof(dst)));

    /* a read starting past the arena is refused and clears the destination */
    memset(dst, PATTERN, sizeof(dst));
    assert(!memory_read(mem, dst, ARENA_SIZE, sizeof(dst)));
    assert(all_zero(dst, sizeof(dst)));

    /* so is one that starts inside but runs off the end */
    memset(dst, PATTERN, sizeof(dst));
    assert(!memory_read(mem, dst, ARENA_SIZE - 16, sizeof(dst)));
    assert(all_zero(dst, sizeof(dst)));

    /* the last byte in the arena is still readable */
    assert(memory_read(mem, dst, ARENA_SIZE - 1, 1));

    /* addr + size must not be allowed to wrap back into range */
    memset(dst, PATTERN, sizeof(dst));
    assert(!memory_read(mem, dst, 0xFFFFFFF0, sizeof(dst)));
    assert(all_zero(dst, sizeof(dst)));

    /* writes and fills reject the same ranges */
    assert(!memory_write(mem, ARENA_SIZE, src, sizeof(src)));
    assert(!memory_write(mem, ARENA_SIZE - 16, src, sizeof(src)));
    assert(!memory_write(mem, 0xFFFFFFF0, src, sizeof(src)));
    assert(!memory_fill(mem, ARENA_SIZE - 16, sizeof(src), 0));
    assert(!memory_fill(mem, 0xFFFFFFF0, sizeof(src), 0));

    memory_delete(mem);
    memory_delete(NULL);
    assert(memory_get_usage() == 0);
    assert(memory_ifetch(0) == 0);
    memory_gc();
    mem = memory_new(ARENA_SIZE);
    assert(mem);
    assert(mem->mem_base[0] == 0);
    memory_delete(mem);
#if HAVE_MMAP
    test_active_chunk_fault(false);
    test_active_chunk_fault(true);
#endif
    printf("io: guest memory bounds enforced\n");
    return 0;
}
