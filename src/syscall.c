/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "state.h"
#include "utils.h"

#define PREALLOC_SIZE 4096
/* newlib is a portable (not RISC-V specific) C library, which implements
 * printf(3) and other functions described in C standards. Some system calls
 * should be provided in conjunction with newlib.
 *
 * system call: name, number
 */
/* clang-format off */
#define SUPPORTED_SYSCALLS           \
    _(close,                57)      \
    _(lseek,                62)      \
    _(read,                 63)      \
    _(write,                64)      \
    _(fstat,                80)      \
    _(exit,                 93)      \
    _(gettimeofday,         169)     \
    _(brk,                  214)     \
    _(clock_gettime,        403)     \
    _(open,                 1024)    \
    IIF(RV32_HAS(SDL))(              \
        _(draw_frame,       0xBEEF)  \
        _(setup_queue,      0xC0DE)  \
        _(submit_queue,     0xFEED)  \
        _(setup_audio,      0xBABE)  \
        _(control_audio,    0xD00D), \
    )
/* clang-format on */

enum {
#define _(name, number) SYS_##name = number,
    SUPPORTED_SYSCALLS
#undef _
};

enum {
    O_RDONLY = 0,
    O_WRONLY = 1,
    O_RDWR = 2,
    O_ACCMODE = 3,
};

static int find_free_fd(state_t *s)
{
    for (int i = 3;; ++i) {
        map_iter_t it;
        map_find(s->fd_map, &it, &i);
        if (map_at_end(s->fd_map, &it))
            return i;
    }
}

static const char *get_mode_str(uint32_t flags, uint32_t mode UNUSED)
{
    switch (flags & O_ACCMODE) {
    case O_RDONLY:
        return "rb";
    case O_WRONLY:
        return "wb";
    case O_RDWR:
        return "a+";
    default:
        return NULL;
    }
}

static uint8_t tmp[PREALLOC_SIZE];
static void syscall_write(riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* _write(fde, buffer, count) */
    riscv_word_t fd = rv_get_reg(rv, rv_reg_a0);
    riscv_word_t buffer = rv_get_reg(rv, rv_reg_a1);
    riscv_word_t count = rv_get_reg(rv, rv_reg_a2);

    /* lookup the file descriptor */
    map_iter_t it;
    map_find(s->fd_map, &it, &fd);
    uint32_t total_write = 0;

    while (count > PREALLOC_SIZE) {
        memory_read(s->mem, tmp, buffer + total_write, PREALLOC_SIZE);
        if (!map_at_end(s->fd_map, &it)) {
            /* write out the data */
            size_t written =
                fwrite(tmp, 1, PREALLOC_SIZE, map_iter_value(&it, FILE *));
            total_write += written;
            count -= PREALLOC_SIZE;
        } else
            goto error_handler;
    }
    memory_read(s->mem, tmp, buffer + total_write, count);
    if (!map_at_end(s->fd_map, &it)) {
        /* write out the data */
        size_t written = fwrite(tmp, 1, count, map_iter_value(&it, FILE *));
        total_write += written;
    } else
        goto error_handler;
    assert(total_write == rv_get_reg(rv, rv_reg_a2));
    /* return number of bytes written */
    rv_set_reg(rv, rv_reg_a0, total_write);
    return;
    /* read the string being printed */
error_handler:
    /* error */
    rv_set_reg(rv, rv_reg_a0, -1);
}


static void syscall_exit(riscv_t *rv)
{
    rv_halt(rv);

    /* To avoid mixing with JSON output */
    if (rv_enables_to_output_exit_code(rv)) {
        riscv_word_t code = rv_get_reg(rv, rv_reg_a0);
        fprintf(stdout, "inferior exit code %d\n", (int) code);
    }
}

/* brk(increment)
 * Note:
 *   - 8 byte alignment for malloc chunks
 *   - 4 KiB aligned for sbrk blocks
 */
static void syscall_brk(riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* get the increment parameter */
    riscv_word_t increment = rv_get_reg(rv, rv_reg_a0);
    if (increment)
        s->break_addr = increment;

    /* return new break address */
    rv_set_reg(rv, rv_reg_a0, s->break_addr);
}

static void syscall_gettimeofday(riscv_t *rv)
{
    /* get the parameters */
    riscv_word_t tv = rv_get_reg(rv, rv_reg_a0);
    riscv_word_t tz = rv_get_reg(rv, rv_reg_a1);

    /* return the clock time */
    if (tv) {
        struct timeval tv_s;
        rv_gettimeofday(&tv_s);
        memory_write_w(tv + 0, (const uint8_t *) &tv_s.tv_sec);
        memory_write_w(tv + 8, (const uint8_t *) &tv_s.tv_usec);
    }

    if (tz) {
        /* FIXME: This parameter is ignored by the syscall handler in newlib. */
    }

    /* success */
    rv_set_reg(rv, rv_reg_a0, 0);
}

static void syscall_clock_gettime(riscv_t *rv)
{
    /* get the parameters */
    riscv_word_t id = rv_get_reg(rv, rv_reg_a0);
    riscv_word_t tp = rv_get_reg(rv, rv_reg_a1);

    switch (id) {
    case CLOCK_REALTIME:
#ifdef CLOCK_MONOTONIC
    case CLOCK_MONOTONIC:
#endif
        break;
    default:
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    if (tp) {
        struct timespec tp_s;
        rv_clock_gettime(&tp_s);
        memory_write_w(tp + 0, (const uint8_t *) &tp_s.tv_sec);
        memory_write_w(tp + 8, (const uint8_t *) &tp_s.tv_nsec);
    }

    /* success */
    rv_set_reg(rv, rv_reg_a0, 0);
}

static void syscall_close(riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* _close(fd); */
    uint32_t fd = rv_get_reg(rv, rv_reg_a0);

    if (fd >= 3) { /* lookup the file descriptor */
        map_iter_t it;
        map_find(s->fd_map, &it, &fd);
        if (!map_at_end(s->fd_map, &it)) {
            fclose(map_iter_value(&it, FILE *));
            map_erase(s->fd_map, &it);

            /* success */
            rv_set_reg(rv, rv_reg_a0, 0);
        }
    }

    /* success */
    rv_set_reg(rv, rv_reg_a0, 0);
}

/* lseek() repositions the file offset of the open file description associated
 * with the file descriptor fd to the argument offset according to the
 * directive whence.
 */
static void syscall_lseek(riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* _lseek(fd, offset, whence); */
    uint32_t fd = rv_get_reg(rv, rv_reg_a0);
    uint32_t offset = rv_get_reg(rv, rv_reg_a1);
    uint32_t whence = rv_get_reg(rv, rv_reg_a2);

    /* find the file descriptor */
    map_iter_t it;
    map_find(s->fd_map, &it, &fd);
    if (map_at_end(s->fd_map, &it)) {
        /* error */
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    FILE *handle = map_iter_value(&it, FILE *);
    if (fseek(handle, offset, whence)) {
        /* error */
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    /* success */
    rv_set_reg(rv, rv_reg_a0, 0);
}

static void syscall_read(riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* _read(fd, buf, count); */
    uint32_t fd = rv_get_reg(rv, rv_reg_a0);
    uint32_t buf = rv_get_reg(rv, rv_reg_a1);
    uint32_t count = rv_get_reg(rv, rv_reg_a2);

    /* lookup the file */
    map_iter_t it;
    map_find(s->fd_map, &it, &fd);
    if (map_at_end(s->fd_map, &it)) {
        /* error */
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    FILE *handle = map_iter_value(&it, FILE *);
    uint32_t total_read = 0;
    /* read the file into runtime memory */

    while (count > PREALLOC_SIZE) {
        size_t r = fread(tmp, 1, PREALLOC_SIZE, handle);
        memory_write(s->mem, buf + total_read, tmp, r);
        count -= PREALLOC_SIZE;
        total_read += PREALLOC_SIZE;
    }
    size_t r = fread(tmp, 1, count, handle);
    memory_write(s->mem, buf + total_read, tmp, r);
    total_read += r;
    assert(total_read == rv_get_reg(rv, rv_reg_a2));
    /* success */
    rv_set_reg(rv, rv_reg_a0, total_read);
}

static void syscall_fstat(riscv_t *rv UNUSED)
{
    /* FIXME: fill real implementation */
}

static void syscall_open(riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* _open(name, flags, mode); */
    uint32_t name = rv_get_reg(rv, rv_reg_a0);
    uint32_t flags = rv_get_reg(rv, rv_reg_a1);
    uint32_t mode = rv_get_reg(rv, rv_reg_a2);

    /* read name from runtime memory */
    char name_str[256] = {'\0'};
    uint32_t read =
        memory_read_str(s->mem, (uint8_t *) name_str, name, sizeof(name_str));
    if (read > sizeof(name_str)) {
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    /* open the file */
    const char *mode_str = get_mode_str(flags, mode);
    if (!mode_str) {
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    FILE *handle = fopen(name_str, mode_str);
    if (!handle) {
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    const int fd = find_free_fd(s); /* find a free file descriptor */

    /* insert into the file descriptor map */
    map_insert(s->fd_map, (void *) &fd, &handle);

    /* return the file descriptor */
    rv_set_reg(rv, rv_reg_a0, fd);
}

#if RV32_HAS(SDL)
extern void syscall_draw_frame(riscv_t *rv);
extern void syscall_setup_queue(riscv_t *rv);
extern void syscall_submit_queue(riscv_t *rv);
extern void syscall_setup_audio(riscv_t *rv);
extern void syscall_control_audio(riscv_t *rv);
#endif

void syscall_handler(riscv_t *rv)
{
    /* get the syscall number */
    riscv_word_t syscall = rv_get_reg(rv, rv_reg_a7);

    switch (syscall) { /* dispatch system call */
#define _(name, number)     \
    case SYS_##name:        \
        syscall_##name(rv); \
        break;
        SUPPORTED_SYSCALLS
#undef _
    default:
        fprintf(stderr, "unknown syscall %d\n", (int) syscall);
        break;
    }
}
