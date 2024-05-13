/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "riscv.h"
#include "riscv_private.h"
#include "utils.h"

#define PREALLOC_SIZE 4096

/* newlib is a portable (not RISC-V specific) C library, which implements
 * printf(3) and other functions described in C standards. Some system calls
 * should be provided in conjunction with newlib.
 *
 * system call: name, number
 */
/* clang-format off */
#define SUPPORTED_SYSCALLS                 \
    _(close,                57)            \
    _(lseek,                62)            \
    _(read,                 63)            \
    _(write,                64)            \
    _(fstat,                80)            \
    _(exit,                 93)            \
    _(gettimeofday,         169)           \
    _(brk,                  214)           \
    _(clock_gettime,        403)           \
    _(open,                 1024)          \
    _(sbi_base,             0x10)          \
    _(sbi_timer,            0x54494D45)    \
    _(sbi_rst,              0x53525354)    \
    IIF(RV32_HAS(SDL))(                    \
        _(draw_frame,       0xBEEF)        \
        _(setup_queue,      0xC0DE)        \
        _(submit_queue,     0xFEED)        \
        _(setup_audio,      0xBABE)        \
        _(control_audio,    0xD00D)        \
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

static int find_free_fd(vm_attr_t *attr)
{
    for (int i = 3;; ++i) {
        map_iter_t it;
        map_find(attr->fd_map, &it, &i);
        if (map_at_end(attr->fd_map, &it))
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
    vm_attr_t *attr = PRIV(rv);

    /* _write(fd, buffer, count) */
    riscv_word_t fd = rv_get_reg(rv, rv_reg_a0);
    riscv_word_t buffer = rv_get_reg(rv, rv_reg_a1);
    riscv_word_t count = rv_get_reg(rv, rv_reg_a2);

    /* lookup the file descriptor */
    map_iter_t it;
    map_find(attr->fd_map, &it, &fd);
    if (map_at_end(attr->fd_map, &it))
        goto error_handler;

    uint32_t total_write = 0;
    FILE *handle = map_iter_value(&it, FILE *);

    while (count > PREALLOC_SIZE) {
        memory_read(attr->mem, tmp, buffer + total_write, PREALLOC_SIZE);
        /* write out the data */
        size_t written = fwrite(tmp, 1, PREALLOC_SIZE, handle);
        if (written != PREALLOC_SIZE && ferror(handle))
            goto error_handler;
        total_write += written;
        count -= PREALLOC_SIZE;
    }

    memory_read(attr->mem, tmp, buffer + total_write, count);
    /* write out the data */
    size_t written = fwrite(tmp, 1, count, handle);
    if (written != count && ferror(handle))
        goto error_handler;
    total_write += written;
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
    /* simply halt cpu and save exit code.
     * the application decides the usage of exit code
     */
    rv_halt(rv);

    vm_attr_t *attr = PRIV(rv);
    attr->exit_code = rv_get_reg(rv, rv_reg_a0);
}

/* brk(increment)
 * Note:
 *   - 8 byte alignment for malloc chunks
 *   - 4 KiB aligned for sbrk blocks
 */
static void syscall_brk(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    /* get the increment parameter */
    riscv_word_t increment = rv_get_reg(rv, rv_reg_a0);
    if (increment)
        attr->break_addr = increment;

    /* return new break address */
    rv_set_reg(rv, rv_reg_a0, attr->break_addr);
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
    vm_attr_t *attr = PRIV(rv);

    /* _close(fd); */
    uint32_t fd = rv_get_reg(rv, rv_reg_a0);

    if (fd >= 3) { /* lookup the file descriptor */
        map_iter_t it;
        map_find(attr->fd_map, &it, &fd);
        if (!map_at_end(attr->fd_map, &it)) {
            if (fclose(map_iter_value(&it, FILE *))) {
                /* error */
                rv_set_reg(rv, rv_reg_a0, -1);
                return;
            }
            map_erase(attr->fd_map, &it);

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
    vm_attr_t *attr = PRIV(rv);

    /* _lseek(fd, offset, whence); */
    uint32_t fd = rv_get_reg(rv, rv_reg_a0);
    uint32_t offset = rv_get_reg(rv, rv_reg_a1);
    uint32_t whence = rv_get_reg(rv, rv_reg_a2);

    /* find the file descriptor */
    map_iter_t it;
    map_find(attr->fd_map, &it, &fd);
    if (map_at_end(attr->fd_map, &it)) {
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

    long pos = ftell(handle);
    if (pos == -1) {
        /* error */
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    /* success */
    rv_set_reg(rv, rv_reg_a0, pos);
}

static void syscall_read(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    /* _read(fd, buf, count); */
    uint32_t fd = rv_get_reg(rv, rv_reg_a0);
    uint32_t buf = rv_get_reg(rv, rv_reg_a1);
    uint32_t count = rv_get_reg(rv, rv_reg_a2);

    /* lookup the file */
    map_iter_t it;
    map_find(attr->fd_map, &it, &fd);
    if (map_at_end(attr->fd_map, &it)) {
        /* error */
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }

    FILE *handle = map_iter_value(&it, FILE *);
    uint32_t total_read = 0;
    /* read the file into runtime memory */

    while (count > PREALLOC_SIZE) {
        size_t r = fread(tmp, 1, PREALLOC_SIZE, handle);
        memory_write(attr->mem, buf + total_read, tmp, r);
        count -= r;
        total_read += r;
        if (r != PREALLOC_SIZE)
            break;
    }
    size_t r = fread(tmp, 1, count, handle);
    memory_write(attr->mem, buf + total_read, tmp, r);
    total_read += r;
    if (total_read != rv_get_reg(rv, rv_reg_a2) && ferror(handle)) {
        /* error */
        rv_set_reg(rv, rv_reg_a0, -1);
        return;
    }
    /* success */
    rv_set_reg(rv, rv_reg_a0, total_read);
}

static void syscall_fstat(riscv_t *rv UNUSED)
{
    /* FIXME: fill real implementation */
}

static void syscall_open(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    /* _open(name, flags, mode); */
    uint32_t name = rv_get_reg(rv, rv_reg_a0);
    uint32_t flags = rv_get_reg(rv, rv_reg_a1);
    uint32_t mode = rv_get_reg(rv, rv_reg_a2);

    /* read name from runtime memory */
    const size_t name_len = strlen((char *) attr->mem->mem_base + name);
    char *name_str = malloc(name_len + 1);
    assert(name_str);
    name_str[name_len] = '\0';
    memory_read(attr->mem, (uint8_t *) name_str, name, name_len);

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

    free(name_str);

    const int fd = find_free_fd(attr); /* find a free file descriptor */

    /* insert into the file descriptor map */
    map_insert(attr->fd_map, (void *) &fd, &handle);

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

/* SBI related system calls */
static void syscall_sbi_timer(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    const riscv_word_t fid = rv_get_reg(rv, rv_reg_a6);
    const riscv_word_t a0 = rv_get_reg(rv, rv_reg_a0);
    const riscv_word_t a1 = rv_get_reg(rv, rv_reg_a1);

    switch (fid) {
    case SBI_TIMER_SET_TIMER:
        attr->timer = (((uint64_t) a1) << 32) | (uint64_t) (a0);
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, 0);
        break;
    default:
        rv_set_reg(rv, rv_reg_a0, SBI_ERR_NOT_SUPPORTED);
        rv_set_reg(rv, rv_reg_a1, 0);
        break;
    }
}

#define SBI_IMPL_ID 0x999
#define SBI_IMPL_VERSION 1

static void syscall_sbi_base(riscv_t *rv)
{
    const riscv_word_t fid = rv_get_reg(rv, rv_reg_a6);

    switch (fid) {
    case SBI_BASE_GET_SBI_IMPL_ID:
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, SBI_IMPL_ID);
        break;
    case SBI_BASE_GET_SBI_IMPL_VERSION:
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, SBI_IMPL_VERSION);
        break;
    case SBI_BASE_GET_MVENDORID:
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, rv->csr_mvendorid);
        break;
    case SBI_BASE_GET_MARCHID:
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, rv->csr_marchid);
        break;
    case SBI_BASE_GET_MIMPID:
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, rv->csr_mimpid);
        break;
    case SBI_BASE_GET_SBI_SPEC_VERSION:
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, (0 << 24) | 3); /* version 0.3 */
        break;
    case SBI_BASE_PROBE_EXTENSION: {
        const riscv_word_t eid = rv_get_reg(rv, rv_reg_a0);
        bool available =
            eid == SBI_EID_BASE || eid == SBI_EID_TIMER || eid == SBI_EID_RST;
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, available);
        break;
    }
    default:
        rv_set_reg(rv, rv_reg_a0, SBI_ERR_NOT_SUPPORTED);
        rv_set_reg(rv, rv_reg_a1, 0);
        break;
    }
}

static void syscall_sbi_rst(riscv_t *rv)
{
    const riscv_word_t fid = rv_get_reg(rv, rv_reg_a6);
    const riscv_word_t a0 = rv_get_reg(rv, rv_reg_a0);
    const riscv_word_t a1 = rv_get_reg(rv, rv_reg_a1);

    switch (fid) {
    case SBI_RST_SYSTEM_RESET:
        fprintf(stderr, "system reset: type=%u, reason=%u\n", a0, a1);
        rv_halt(rv);
        rv_set_reg(rv, rv_reg_a0, SBI_SUCCESS);
        rv_set_reg(rv, rv_reg_a1, 0);
        break;
    default:
        rv_set_reg(rv, rv_reg_a0, SBI_ERR_NOT_SUPPORTED);
        rv_set_reg(rv, rv_reg_a1, 0);
        break;
    }
}

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

    /* save return code.
     * the application decides the usage of the return code
     */
    vm_attr_t *attr = PRIV(rv);
    attr->error = rv_get_reg(rv, rv_reg_a0);
}
