/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#if RV32_HAS(SYSTEM_MMIO)
#include <termios.h>
#include "dtc/libfdt/libfdt.h"
#endif

#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>
#define FILENO(x) fileno(x)
#else
#define FILENO(x) _fileno(x)
#define STDIN_FILENO FILENO(stdin)
#define STDOUT_FILENO FILENO(stdout)
#define STDERR_FILENO FILENO(stderr)
#endif

#if defined(__EMSCRIPTEN__)
#include "em_runtime.h"
#endif

#include "elf.h"
#include "mpool.h"
#include "riscv.h"
#include "riscv_private.h"
#include "utils.h"
#if RV32_HAS(JIT)
#if RV32_HAS(T2C)
#include <pthread.h>
#endif
#include "cache.h"
#include "jit.h"
#define CODE_CACHE_SIZE (4 * 1024 * 1024)
#endif

#define BLOCK_IR_MAP_CAPACITY_BITS 10

#if !RV32_HAS(JIT)
/* initialize the block map */
static void block_map_init(block_map_t *map, const uint8_t bits)
{
    map->block_capacity = 1 << bits;
    map->size = 0;
    map->map = calloc(map->block_capacity, sizeof(struct block *));
    assert(map->map);
}

/* clear all block in the block map */
void block_map_clear(riscv_t *rv)
{
    block_map_t *map = &rv->block_map;
    for (uint32_t i = 0; i < map->block_capacity; i++) {
        block_t *block = map->map[i];
        if (!block)
            continue;

        uint32_t idx;
        rv_insn_t *ir, *next;
        for (idx = 0, ir = block->ir_head; idx < block->n_insn;
             idx++, ir = next) {
            if (ir->fuse)
                mpool_free(rv->fuse_mp, ir->fuse);
            free(ir->branch_table);
            next = ir->next;
            mpool_free(rv->block_ir_mp, ir);
        }
        mpool_free(rv->block_mp, block);
        map->map[i] = NULL;
    }
    map->size = 0;
}

static void block_map_destroy(riscv_t *rv)
{
    block_map_clear(rv);
    free(rv->block_map.map);

    mpool_destroy(rv->block_mp);
    mpool_destroy(rv->block_ir_mp);
    mpool_destroy(rv->fuse_mp);
}
#endif

bool rv_set_pc(riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
#if RV32_HAS(EXT_C)
    if (pc & 1)
#else
    if (pc & 3)
#endif
        return false;

    rv->PC = pc;
    return true;
}

riscv_word_t rv_get_pc(riscv_t *rv)
{
    assert(rv);
    return rv->PC;
}

void rv_set_reg(riscv_t *rv, uint32_t reg, riscv_word_t in)
{
    assert(rv);
    if (reg < N_RV_REGS && reg != rv_reg_zero)
        rv->X[reg] = in;
}

riscv_word_t rv_get_reg(riscv_t *rv, uint32_t reg)
{
    assert(rv);
    if (reg < N_RV_REGS)
        return rv->X[reg];

    return ~0U;
}

/* Remap standard stream
 *
 * @rv: riscv
 * @fsp: a list of pair of mapping from fd to FILE *
 * @fsp_size: list size
 *
 * Note: fd inside fsp should be 0 or 1 or 2 only
 */
void rv_remap_stdstream(riscv_t *rv, fd_stream_pair_t *fsp, uint32_t fsp_size)
{
    assert(rv);

    vm_attr_t *attr = PRIV(rv);
    assert(attr && attr->fd_map);

    for (uint32_t i = 0; i < fsp_size; i++) {
        int fd = fsp[i].fd;
        FILE *file = fsp[i].file;

        if (!file)
            continue;
        if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO)
            continue;

        /* check if standard stream refered by fd exists or not */
        map_iter_t it;
        map_find(attr->fd_map, &it, &fd);
        if (it.node) /* found, remove first */
            map_erase(attr->fd_map, &it);
        map_insert(attr->fd_map, &fd, &file);

        /* store new fd to make the vm_attr_t consistent */
        int new_fd = FILENO(file);
        assert(new_fd != -1);

        if (fd == STDIN_FILENO)
            attr->fd_stdin = new_fd;
        else if (fd == STDOUT_FILENO) {
            attr->fd_stdout = new_fd;
            rv_log_set_stdout_stream(file);
        } else
            attr->fd_stderr = new_fd;
    }
}

#define MEMIO(op) on_mem_##op
#define IO_HANDLER_IMPL(type, op, RW)                                  \
    static IIF(RW)(                                                    \
        /* W */ void MEMIO(op)(UNUSED riscv_t * rv, riscv_word_t addr, \
                               riscv_##type##_t data),                 \
        /* R */ riscv_##type##_t MEMIO(op)(UNUSED riscv_t * rv,        \
                                           riscv_word_t addr))         \
    {                                                                  \
        IIF(RW)(memory_##op(addr, (uint8_t *) &data),                  \
                return memory_##op(addr));                             \
    }

#if !RV32_HAS(SYSTEM)
#define R 0
#define W 1

IO_HANDLER_IMPL(word, ifetch, R)
IO_HANDLER_IMPL(word, read_w, R)
IO_HANDLER_IMPL(half, read_s, R)
IO_HANDLER_IMPL(byte, read_b, R)

IO_HANDLER_IMPL(word, write_w, W)
IO_HANDLER_IMPL(half, write_s, W)
IO_HANDLER_IMPL(byte, write_b, W)

#undef R
#undef W
#endif

#if RV32_HAS(T2C)
static pthread_t t2c_thread;
static void *t2c_runloop(void *arg)
{
    riscv_t *rv = (riscv_t *) arg;
    pthread_mutex_lock(&rv->wait_queue_lock);
    while (!rv->quit) {
        /* Wait for work or quit signal */
        while (list_empty(&rv->wait_queue) && !rv->quit)
            pthread_cond_wait(&rv->wait_queue_cond, &rv->wait_queue_lock);

        if (rv->quit)
            break;

        /* Extract work item while holding the lock */
        queue_entry_t *entry =
            list_last_entry(&rv->wait_queue, queue_entry_t, list);
        list_del_init(&entry->list);
        pthread_mutex_unlock(&rv->wait_queue_lock);

        /* Perform compilation with cache lock */
        pthread_mutex_lock(&rv->cache_lock);
        /* Look up block from cache using the key (might have been evicted) */
        uint32_t pc = (uint32_t) entry->key;
        block_t *block = (block_t *) cache_get(rv->block_cache, pc, false);
#if RV32_HAS(SYSTEM)
        /* Verify SATP matches (for system mode) */
        uint32_t satp = (uint32_t) (entry->key >> 32);
        if (block && block->satp != satp)
            block = NULL;
#endif
        /* Compile only if block still exists in cache */
        if (block)
            t2c_compile(rv, block);
        pthread_mutex_unlock(&rv->cache_lock);
        free(entry);

        pthread_mutex_lock(&rv->wait_queue_lock);
    }
    pthread_mutex_unlock(&rv->wait_queue_lock);
    return NULL;
}
#endif

#if RV32_HAS(SYSTEM_MMIO)
/* Map a file into memory at the specified location.
 * If max_size > 0, validates that file size does not exceed max_size.
 * Returns the actual file size on success, or -1 when file exceeds max_size
 * (caller handles the error message). Other errors cause program exit.
 */
static off_t map_file(char **ram_loc, const char *name, off_t max_size)
{
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        goto fail;

    /* get file size */
    struct stat st;
    if (fstat(fd, &st) < 0)
        goto cleanup;

    /* Validate file size if max_size constraint is specified */
    if (max_size > 0 && st.st_size > max_size) {
        close(fd);
        return -1; /* Caller handles the error message */
    }

#if HAVE_MMAP
    /* Remap file to memory region. Emscripten/Windows use fallback read path
     * since they don't support mmap with location hints.
     */
    *ram_loc = mmap(*ram_loc, st.st_size, PROT_READ | PROT_WRITE,
                    MAP_FIXED | MAP_PRIVATE, fd, 0);
    if (*ram_loc == MAP_FAILED)
        goto cleanup;
#else
    if (read(fd, *ram_loc, st.st_size) != st.st_size) {
        free(*ram_loc);
        goto cleanup;
    }
#endif

    /*
     * The kernel selects a nearby page boundary and attempts to create
     * the mapping.
     */
    *ram_loc += st.st_size;
    close(fd);
    return st.st_size;

cleanup:
    close(fd);
fail:
    rv_log_fatal("map_file() %s failed: %s", name, strerror(errno));
    exit(EXIT_FAILURE);
}

#define ALIGN_FDT(x) (((x) + (FDT_TAGSIZE) - 1) & ~((FDT_TAGSIZE) - 1))
static char *realloc_property(char *fdt,
                              int nodeoffset,
                              const char *name,
                              int newlen)
{
    int delta = 0;
    int oldlen = 0;

    if (!fdt_get_property(fdt, nodeoffset, name, &oldlen))
        /* strings + property header */
        delta = sizeof(struct fdt_property) + strlen(name) + 1;

    if (newlen > oldlen)
        /* actual value in off_struct */
        delta += ALIGN_FDT(newlen) - ALIGN_FDT(oldlen);

    int new_sz = fdt_totalsize(fdt) + delta;
    /* Assume the pre-allocated RAM is enough here, so we
     * don't realloc any memory for fdt */
    fdt_open_into(fdt, fdt, new_sz);
    return fdt;
}

static void load_dtb(char **ram_loc, vm_attr_t *attr)
{
#include "minimal_dtb.h"
    char *bootargs = attr->data.system.bootargs;
    char **vblk = attr->data.system.vblk_device;
    char *blob = *ram_loc;
    char *buf;
    size_t len;
    int node, err;
    int totalsize;

#define DTB_EXPAND_SIZE 1024 /* or more if needed */

    /* Allocate enough memory for DTB + extra room */
    size_t minimal_len = ARRAY_SIZE(minimal);
    void *dtb_buf = calloc(minimal_len + DTB_EXPAND_SIZE, sizeof(uint8_t));
    assert(dtb_buf);

    /* Expand it to a usable DTB blob */
    err = fdt_open_into(minimal, dtb_buf, minimal_len + DTB_EXPAND_SIZE);
    if (err < 0) {
        rv_log_error("fdt_open_into fails\n");
        exit(EXIT_FAILURE);
    }

    if (bootargs) {
        node = fdt_path_offset(dtb_buf, "/chosen");
        assert(node > 0);

        len = strlen(bootargs);
        buf = malloc(len + 1);
        assert(buf);
        memcpy(buf, bootargs, len);
        buf[len] = 0;
        err = fdt_setprop(dtb_buf, node, "bootargs", buf, len + 1);
        if (err == -FDT_ERR_NOSPACE) {
            dtb_buf = realloc_property(dtb_buf, node, "bootargs", len);
            err = fdt_setprop(dtb_buf, node, "bootargs", buf, len);
        }
        free(buf);
        assert(!err);
    }

/* Remove the rtc node if it is not enabled during compile time */
#if !RV32_HAS(GOLDFISH_RTC)
    const char *rtc_path = fdt_get_alias(dtb_buf, "rtc0");
    assert(rtc_path);

    node = fdt_path_offset(dtb_buf, rtc_path);
    assert(node > 0);

    err = fdt_del_node(dtb_buf, node);
    if (err < 0)
        rv_log_warn("Failed to remove rtc node from DTB");
#endif

    if (vblk) {
        int node = fdt_path_offset(dtb_buf, "/soc@F0000000");
        assert(node >= 0);

        uint32_t base_addr = 0x4000000;
        uint32_t addr_offset = 0x100000;
        uint32_t size = 0x200;

        uint32_t next_addr = base_addr;
        uint32_t next_irq = 1;

        /* scan existing nodes to get next addr and irq */
        int subnode;
        fdt_for_each_subnode(subnode, dtb_buf, node)
        {
            const char *name = fdt_get_name(dtb_buf, subnode, NULL);
            assert(name);

            char *at_pos = strchr(name, '@');
            assert(at_pos);

            char *endptr;
            uint32_t addr = strtoul(at_pos + 1, &endptr, 16);
            if (endptr == at_pos + 1) {
                attr->vblk_cnt = 0;
                rv_log_error(
                    "Invalid unit-address in node: %s, skipping virtio blocks "
                    "MMIO",
                    name);
                goto dtb_end;
            }
            if (addr == next_addr)
                next_addr = addr + addr_offset;

            const fdt32_t *irq_prop =
                fdt_getprop(dtb_buf, subnode, "interrupts", NULL);
            if (irq_prop) {
                uint32_t irq = fdt32_to_cpu(*irq_prop);
                if (irq == next_irq)
                    next_irq = irq + 1;
            }
        }
        /* set IRQ for virtio block, see devices/virtio.h */
        attr->vblk_irq_base = next_irq;

        /* set the VBLK MMIO valid range */
        attr->vblk_mmio_base_hi = next_addr >> 20;
        attr->vblk_mmio_max_hi = attr->vblk_mmio_base_hi + attr->vblk_cnt;

        /* adding new virtio block nodes */
        for (int i = 0; i < attr->vblk_cnt; i++) {
            uint32_t new_addr = next_addr + i * addr_offset;
            uint32_t new_irq = next_irq + i;

            char node_name[32];
            snprintf(node_name, sizeof(node_name), "virtio@%x", new_addr);

            int subnode = fdt_add_subnode(dtb_buf, node, node_name);
            if (subnode == -FDT_ERR_NOSPACE) {
                rv_log_warn("add subnode no space!\n");
            }
            assert(subnode >= 0);

            /* compatible = "virtio,mmio" */
            assert(fdt_setprop_string(dtb_buf, subnode, "compatible",
                                      "virtio,mmio") == 0);

            /* reg = <new_addr size> */
            uint32_t reg[2] = {cpu_to_fdt32(new_addr), cpu_to_fdt32(size)};
            assert(fdt_setprop(dtb_buf, subnode, "reg", reg, sizeof(reg)) == 0);

            /* interrupts = <new_irq> */
            uint32_t irq = cpu_to_fdt32(new_irq);
            assert(fdt_setprop(dtb_buf, subnode, "interrupts", &irq,
                               sizeof(irq)) == 0);
        }
    }

dtb_end:
    memcpy(blob, dtb_buf, minimal_len + DTB_EXPAND_SIZE);
    free(dtb_buf);

    totalsize = fdt_totalsize(blob);
    *ram_loc += totalsize;
    return;
}

/*
 * The control mode flag for keyboard.
 *
 * ICANON: Enable canonical mode.
 * ECHO: Echo input characters.
 * ISIG: When any of the characters INTR, QUIT,
 *       SUSP, or DSUSP are received, generate the
 *       corresponding signal.
 *
 * It is essential to re-enable ISIG upon exit.
 * Otherwise, the default signal handler will
 * not catch the signal. E.g., SIGINT generated by
 * CTRL + c.
 *
 */
#define TERMIOS_C_CFLAG (ICANON | ECHO | ISIG)
static void reset_keyboard_input()
{
    struct termios term;
    tcgetattr(0, &term);
    term.c_lflag |= TERMIOS_C_CFLAG;
    tcsetattr(0, TCSANOW, &term);
}

/* Asynchronous communication to capture all keyboard input for the VM. */
static void capture_keyboard_input()
{
    /* Hook exit, because we want to re-enable default control modes. */
    atexit(reset_keyboard_input);

    struct termios term;
    tcgetattr(0, &term);
    term.c_lflag &= ~TERMIOS_C_CFLAG;
    tcsetattr(0, TCSANOW, &term);
}

#endif

#if RV32_HAS(SYSTEM_MMIO)
/*
 *
 * atexit() registers void (*)(void) callbacks, so no parameters can be passed.
 * Memory must be freed at runtime. block_map_clear() requires a RISC-V instance
 * and runs in interpreter mode. Instead of modifying its signature, access the
 * global RISC-V instance in main.c with external linkage.
 *
 */
extern riscv_t *rv;
static void rv_async_block_clear()
{
#if !RV32_HAS(JIT)
    if (rv && rv->block_map.size)
        block_map_clear(rv);
#else  /* TODO: JIT mode */
    return;
#endif /* !RV32_HAS(JIT) */
}

static void rv_fsync_device()
{
    if (!rv)
        return;

    vm_attr_t *attr = PRIV(rv);
    /*
     * mmap_fallback, may need to write and sync the device
     *
     * vblk is optional, so it could be NULL
     */
    if (attr->vblk_cnt) {
        for (int i = 0; i < attr->vblk_cnt; i++) {
            virtio_blk_state_t *vblk = attr->vblk[i];
            if (vblk->disk_fd >= 3) {
                if (vblk->device_features & VIRTIO_BLK_F_RO) /* readonly */
                    goto end;

                if (pwrite(vblk->disk_fd, vblk->disk, vblk->disk_size, 0) ==
                    -1) {
                    rv_log_error("pwrite block device failed: %s",
                                 strerror(errno));
                    return;
                }

                if (fsync(vblk->disk_fd) == -1) {
                    rv_log_error("fsync block device failed: %s",
                                 strerror(errno));
                    return;
                }
                rv_log_info("Sync block device OK");

            end:
                close(vblk->disk_fd);
            }

            vblk_delete(vblk);
        }

        free(attr->vblk);
        free(attr->disk);
    }
}
#endif /* RV32_HAS(SYSTEM_MMIO) */

riscv_t *rv_create(riscv_user_t rv_attr)
{
    assert(rv_attr);

    riscv_t *rv = calloc(1, sizeof(riscv_t));
    if (!rv)
        return NULL;
    assert(rv);

#if RV32_HAS(SYSTEM_MMIO)
    /* register cleaning callback for CTRL+a+x exit */
    atexit(rv_async_block_clear);
    /* register device sync callback for CTRL+a+x exit */
    atexit(rv_fsync_device);
#endif

    /* copy over the attr */
    rv->data = rv_attr;

    vm_attr_t *attr = PRIV(rv);
    attr->mem = memory_new(attr->mem_size);
    assert(attr->mem);
    assert(!(((uintptr_t) attr->mem) & 0b11));

    /* reset */
    rv_reset(rv, 0U);

    /*
     * default standard stream.
     * rv_remap_stdstream() can be called to overwrite them
     *
     * The logging stdout stream will be remapped as well
     *
     */
    attr->fd_map = map_init(int, FILE *, map_cmp_int);
    rv_remap_stdstream(rv,
                       (fd_stream_pair_t[]) {
                           {STDIN_FILENO, stdin},
                           {STDOUT_FILENO, stdout},
                           {STDERR_FILENO, stderr},
                       },
                       3);

    rv_log_set_level(attr->log_level);
    rv_log_info("Log level: %s", rv_log_level_string(attr->log_level));

#if !RV32_HAS(SYSTEM_MMIO)
    elf_t *elf = elf_new();
    assert(elf);

    if (!elf_open(elf, attr->data.user.elf_program)) {
        rv_log_fatal("elf_open() failed");
        map_delete(attr->fd_map);
        memory_delete(attr->mem);
        free(rv);
        exit(EXIT_FAILURE);
    }
    rv_log_info("%s ELF loaded", attr->data.user.elf_program);

    const struct Elf32_Sym *end;
    if ((end = elf_get_symbol(elf, "_end")))
        attr->break_addr = end->st_value;

#if !RV32_HAS(SYSTEM)
    /* set not exiting */
    attr->on_exit = false;

    const struct Elf32_Sym *exit;
    if ((exit = elf_get_symbol(elf, "exit")))
        attr->exit_addr = exit->st_value;
#endif

    assert(elf_load(elf, attr->mem));

    /* set the entry pc */
    const struct Elf32_Ehdr UNUSED *hdr = get_elf_header(elf);
    assert(rv_set_pc(rv, hdr->e_entry));

    elf_delete(elf);

/* combine with USE_ELF for system test suite */
#if RV32_HAS(SYSTEM)
    /* this variable has external linkage to mmu_io defined in system.c */
    extern riscv_io_t mmu_io;
    /* install the MMU I/O handlers */
    memcpy(&rv->io, &mmu_io, sizeof(riscv_io_t));
#else
    /* install the I/O handlers */
    const riscv_io_t io = {
        /* memory read interface */
        .mem_ifetch = MEMIO(ifetch),
        .mem_read_w = MEMIO(read_w),
        .mem_read_s = MEMIO(read_s),
        .mem_read_b = MEMIO(read_b),

        /* memory write interface */
        .mem_write_w = MEMIO(write_w),
        .mem_write_s = MEMIO(write_s),
        .mem_write_b = MEMIO(write_b),

        /* system services or essential routines */
        .on_ecall = ecall_handler,
        .on_ebreak = ebreak_handler,
        .on_memcpy = memcpy_handler,
        .on_memset = memset_handler,
        .on_trap = trap_handler,
    };
    memcpy(&rv->io, &io, sizeof(riscv_io_t));
#endif /* RV32_HAS(SYSTEM) */

#else
    /* *-----------------------------------------*
     * |              Memory layout              |
     * *----------------*----------------*-------*
     * |  kernel image  |  initrd image  |  dtb  |
     * *----------------*----------------*-------*
     */

    /* load_dtb needs the count to add the virtio block subnode dynamically */
    attr->vblk_cnt = attr->data.system.vblk_device_cnt;

    char *ram_loc = (char *) attr->mem->mem_base;
    map_file(&ram_loc, attr->data.system.kernel, 0);
    rv_log_info("Kernel loaded");

    uint32_t dtb_addr = attr->mem->mem_size - DTB_SIZE;
    ram_loc = ((char *) attr->mem->mem_base) + dtb_addr;
    load_dtb(&ram_loc, attr);
    rv_log_info("DTB loaded");
    /* Load optional initrd image before the dtb region.
     * The initrd region size is defined by INITRD_SIZE at compile time.
     */
    if (attr->data.system.initrd) {
        /* Ensure memory is large enough to hold initrd region */
        if (dtb_addr < INITRD_SIZE) {
            rv_log_fatal(
                "Memory too small for INITRD_SIZE (%u MiB). Increase MEM_SIZE.",
                INITRD_SIZE / (1024 * 1024));
            exit(EXIT_FAILURE);
        }
        uint32_t initrd_addr = dtb_addr - INITRD_SIZE;
        ram_loc = ((char *) attr->mem->mem_base) + initrd_addr;
        off_t initrd_size =
            map_file(&ram_loc, attr->data.system.initrd, INITRD_SIZE);
        if (initrd_size < 0) {
            /* map_file returns -1 when file exceeds max_size */
            rv_log_fatal(
                "Initrd file exceeds INITRD_SIZE (%u MiB).\n"
                "Please rebuild with a larger INITRD_SIZE, e.g.:\n"
                "  make ENABLE_SYSTEM=1 INITRD_SIZE=64 system",
                INITRD_SIZE / (1024 * 1024));
            exit(EXIT_FAILURE);
        }
        rv_log_info("Rootfs loaded (%ld bytes)", (long) initrd_size);
    }

    /* this variable has external linkage to mmu_io defined in system.c */
    extern riscv_io_t mmu_io;
    memcpy(&rv->io, &mmu_io, sizeof(riscv_io_t));

    /* setup RISC-V hart */
    rv_set_reg(rv, rv_reg_a0, 0);
    rv_set_reg(rv, rv_reg_a1, dtb_addr);

    /* setup timer */
    attr->timer = 0xFFFFFFFFFFFFFFF;

    /* setup PLIC */
    attr->plic = plic_new();
    assert(attr->plic);
    attr->plic->rv = rv;

    /* setup UART */
    attr->uart = u8250_new();
    assert(attr->uart);
    attr->uart->in_fd = attr->fd_stdin;
    attr->uart->out_fd = attr->fd_stdout;

    /* setup rtc */
#if RV32_HAS(GOLDFISH_RTC)
    attr->rtc = rtc_new();
    assert(attr->rtc);
#endif /* RV32_HAS(GOLDFISH_RTC) */

    attr->vblk = calloc(attr->vblk_cnt, sizeof(virtio_blk_state_t *));
    assert(attr->vblk);
    attr->disk = calloc(attr->vblk_cnt, sizeof(uint32_t *));
    assert(attr->disk);

    if (attr->vblk_cnt) {
        for (int i = 0; i < attr->vblk_cnt; i++) {
/* Currently, only used for block image path and permission */
#define MAX_OPTS 2
            char *vblk_device_str = attr->data.system.vblk_device[i];
            if (!vblk_device_str[0]) {
                rv_log_error("Disk path cannot be empty");
                exit(EXIT_FAILURE);
            }

            char *vblk_opts[MAX_OPTS] = {NULL};
            int vblk_opt_idx = 0;
            char *opt = strtok(vblk_device_str, ",");
            while (opt) {
                if (vblk_opt_idx == MAX_OPTS) {
                    rv_log_error("Too many arguments for vblk");
                    break;
                }
                vblk_opts[vblk_opt_idx++] = opt;
                opt = strtok(NULL, ",");
            }

            char *vblk_device;
            char *vblk_readonly = vblk_opts[1];
            bool readonly = false;

            if (vblk_opts[0][0] == '~') {
                /* HOME environment variable should be common in macOS and Linux
                 * distribution and it is set by the login program
                 */
                const char *home = getenv("HOME");
                if (!home) {
                    rv_log_error(
                        "HOME environment variable is not set, cannot access "
                        "the disk %s",
                        vblk_opts[0]);
                    exit(EXIT_FAILURE);
                }

                const char *suffix = vblk_opts[0] + 1; /* skip ~ */
                size_t home_len = strlen(home);
                size_t suffix_len = strlen(suffix);
                if (home_len > SIZE_MAX - suffix_len - 1) {
                    rv_log_error("Disk path too long");
                    exit(EXIT_FAILURE);
                }
                size_t path_len = home_len + suffix_len + 1;
                vblk_device = malloc(path_len);
                if (!vblk_device) {
                    rv_log_error("Failed to allocate memory for disk path");
                    exit(EXIT_FAILURE);
                }
                snprintf(vblk_device, path_len, "%s%s", home, suffix);
            } else {
                vblk_device = vblk_opts[0];
            }

            if (vblk_readonly) {
                if (strcmp(vblk_readonly, "readonly") != 0) {
                    rv_log_error("Unknown vblk option: %s", vblk_readonly);
                    exit(EXIT_FAILURE);
                }
                readonly = true;
            }

            attr->vblk[i] = vblk_new();
            attr->vblk[i]->ram = (uint32_t *) attr->mem->mem_base;
            attr->disk[i] =
                virtio_blk_init(attr->vblk[i], vblk_device, readonly);

            if (vblk_opts[0][0] == '~')
                free(vblk_device);
        }
    }

    capture_keyboard_input();
#endif /* !RV32_HAS(SYSTEM_MMIO) */

    /* create block and IRs memory pool */
    rv->block_mp = mpool_create(sizeof(block_t) << BLOCK_MAP_CAPACITY_BITS,
                                sizeof(block_t));
    rv->block_ir_mp = mpool_create(
        sizeof(rv_insn_t) << BLOCK_IR_MAP_CAPACITY_BITS, sizeof(rv_insn_t));
    /* Fuse pool: fixed-size slots for macro-op fusion arrays.
     * Each slot holds up to FUSE_MAX_ENTRIES opcode_fuse_t structures.
     */
    rv->fuse_mp = mpool_create(FUSE_SLOT_SIZE << BLOCK_IR_MAP_CAPACITY_BITS,
                               FUSE_SLOT_SIZE);
    if (!rv->block_mp || !rv->block_ir_mp || !rv->fuse_mp) {
        rv_log_fatal("Failed to create memory pool");
        goto fail_mpool;
    }

#if !RV32_HAS(JIT)
    /* initialize the block map */
    block_map_init(&rv->block_map, BLOCK_MAP_CAPACITY_BITS);
#else
    INIT_LIST_HEAD(&rv->block_list);
    rv->jit_state = jit_state_init(CODE_CACHE_SIZE);
    if (!rv->jit_state) {
        rv_log_fatal("Failed to initialize JIT state");
        goto fail_jit_state;
    }
    rv->block_cache = cache_create(BLOCK_MAP_CAPACITY_BITS);
    if (!rv->block_cache) {
        rv_log_fatal("Failed to create block cache");
        goto fail_block_cache;
    }
#if RV32_HAS(T2C)
    rv->quit = false;
    rv->jit_cache = jit_cache_init();
    if (!rv->jit_cache) {
        rv_log_fatal("Failed to initialize JIT cache");
        goto fail_jit_cache;
    }
    /* prepare wait queue. */
    pthread_mutex_init(&rv->wait_queue_lock, NULL);
    pthread_mutex_init(&rv->cache_lock, NULL);
    pthread_cond_init(&rv->wait_queue_cond, NULL);
    INIT_LIST_HEAD(&rv->wait_queue);
    /* activate the background compilation thread. */
    pthread_create(&t2c_thread, NULL, t2c_runloop, rv);
#endif
#endif

    return rv;

#if RV32_HAS(JIT)
#if RV32_HAS(T2C)
fail_jit_cache:
    cache_free(rv->block_cache);
#endif
fail_block_cache:
    if (rv->jit_state)
        jit_state_exit(rv->jit_state);
fail_jit_state:
#endif
fail_mpool:
    mpool_destroy(rv->block_ir_mp);
    mpool_destroy(rv->block_mp);
    mpool_destroy(rv->fuse_mp);
#if RV32_HAS(SYSTEM_MMIO)
    if (attr->uart)
        u8250_delete(attr->uart);
    if (attr->plic)
        plic_delete(attr->plic);
#if RV32_HAS(GOLDFISH_RTC)
    if (attr->rtc)
        rtc_delete(attr->rtc);
#endif
    if (attr->vblk) {
        for (int i = 0; i < attr->vblk_cnt; i++) {
            if (attr->vblk[i])
                vblk_delete(attr->vblk[i]);
        }
        free(attr->vblk);
    }
    free(attr->disk);
#endif
    map_delete(attr->fd_map);
    memory_delete(attr->mem);
    free(rv);
    return NULL;
}

#if !RV32_HAS(SYSTEM_MMIO)
/*
 * TODO: enable to trace Linux kernel symbol
 */
static void rv_run_and_trace(riscv_t *rv)
{
    assert(rv);

    vm_attr_t *attr = PRIV(rv);
    assert(attr && attr->data.user.elf_program);
    attr->cycle_per_step = 1;

    const char UNUSED *prog_name = attr->data.user.elf_program;
    elf_t *elf = elf_new();
    assert(elf && elf_open(elf, prog_name));

    for (; !rv_has_halted(rv);) { /* run until the flag is done */
        /* trace execution */
        uint32_t pc = rv_get_pc(rv);
        const char *sym = elf_find_symbol(elf, pc);
        rv_log_trace("%08x  %s", pc, (sym ? sym : ""));

        rv_step(rv); /* step instructions */
    }

    elf_delete(elf);
}
#endif

#if RV32_HAS(GDBSTUB)
/* Run the RISC-V emulator as gdbstub */
void rv_debug(riscv_t *rv);
#endif

void rv_profile(riscv_t *rv, char *out_file_path);

void rv_run(riscv_t *rv)
{
    assert(rv);

    vm_attr_t *attr = PRIV(rv);
    assert(attr &&
#if RV32_HAS(SYSTEM_MMIO)
           attr->data.system.kernel && attr->data.system.initrd
#else
           attr->data.user.elf_program
#endif
    );

    if (!(attr->run_flag & (RV_RUN_TRACE | RV_RUN_GDBSTUB))) {
#ifdef __EMSCRIPTEN__
        emscripten_set_main_loop_arg(rv_step, (void *) rv, 0, 1);
#else
        /* default main loop */
        for (; !rv_has_halted(rv);) /* run until the flag is done */
            rv_step(rv);            /* step instructions */
#endif
    }
#if !RV32_HAS(SYSTEM_MMIO)
    else if (attr->run_flag & RV_RUN_TRACE)
        rv_run_and_trace(rv);
#endif
#if RV32_HAS(GDBSTUB)
    else if (attr->run_flag & RV_RUN_GDBSTUB)
        rv_debug(rv);
#endif

    if (attr->run_flag & RV_RUN_PROFILE) {
        assert(attr->profile_output_file);
        rv_profile(rv, attr->profile_output_file);
    }
}

void rv_halt(riscv_t *rv)
{
    rv->halt = true;
}

bool rv_has_halted(riscv_t *rv)
{
    return rv->halt;
}

#if RV32_HAS(ARCH_TEST)
void rv_set_tohost_addr(riscv_t *rv, uint32_t addr)
{
    rv->tohost_addr = addr;
}

void rv_set_fromhost_addr(riscv_t *rv, uint32_t addr)
{
    rv->fromhost_addr = addr;
}
#endif

void rv_delete(riscv_t *rv)
{
    assert(rv);
#if !RV32_HAS(JIT) || (RV32_HAS(SYSTEM_MMIO))
    vm_attr_t *attr = PRIV(rv);
#endif
#if !RV32_HAS(JIT)
    map_delete(attr->fd_map);
    memory_delete(attr->mem);
    block_map_destroy(rv);
#else
#if RV32_HAS(T2C)
    /* Signal the thread to quit */
    pthread_mutex_lock(&rv->wait_queue_lock);
    rv->quit = true;
    pthread_cond_signal(&rv->wait_queue_cond);
    pthread_mutex_unlock(&rv->wait_queue_lock);

    pthread_join(t2c_thread, NULL);

    /* Clean up any remaining entries in wait queue */
    queue_entry_t *entry, *safe;
    list_for_each_entry_safe (entry, safe, &rv->wait_queue, list) {
        list_del(&entry->list);
        free(entry);
    }

    pthread_mutex_destroy(&rv->wait_queue_lock);
    pthread_mutex_destroy(&rv->cache_lock);
    pthread_cond_destroy(&rv->wait_queue_cond);
    jit_cache_exit(rv->jit_cache);
#endif
    jit_state_exit(rv->jit_state);
    cache_free(rv->block_cache);
    mpool_destroy(rv->block_ir_mp);
    mpool_destroy(rv->block_mp);
    mpool_destroy(rv->fuse_mp);
#endif
#if RV32_HAS(SYSTEM_MMIO)
    u8250_delete(attr->uart);
    plic_delete(attr->plic);
#if RV32_HAS(GOLDFISH_RTC)
    rtc_delete(attr->rtc);
#endif /* RV32_HAS(GOLDFISH_RTC) */
    /* sync device, cleanup inside the callee */
    rv_fsync_device();
#endif
    free(rv);
}

void rv_reset(riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
    memset(rv->X, 0, sizeof(uint32_t) * N_RV_REGS);

    vm_attr_t *attr = PRIV(rv);
    int argc = attr->argc;
    char **args = attr->argv;
    memory_t *mem = attr->mem;

    /* set the reset address */
    rv->PC = pc;

    /* set the default stack pointer */
    rv->X[rv_reg_sp] =
        attr->mem_size - attr->stack_size - attr->args_offset_size;

    /* Store 'argc' and 'args' of the target program in 'state->mem'. Thus,
     * we can use an offset trick to emulate 32/64-bit target programs on
     * a 64-bit built emulator.
     *
     * memory layout of arguments as below:
     * -----------------------
     * |    NULL            |
     * -----------------------
     * |    envp[n]         |
     * -----------------------
     * |    envp[n - 1]     |
     * -----------------------
     * |    ...             |
     * -----------------------
     * |    envp[0]         |
     * -----------------------
     * |    NULL            |
     * -----------------------
     * |    args[n]         |
     * -----------------------
     * |    args[n - 1]     |
     * -----------------------
     * |    ...             |
     * -----------------------
     * |    args[0]         |
     * -----------------------
     * |    argc            |
     * -----------------------
     *
     * TODO: access to envp
     */

    /* copy args to RAM */
    uintptr_t args_size = (1 + argc + 1) * sizeof(uint32_t);
    uintptr_t args_bottom = attr->mem_size - attr->stack_size;
    uintptr_t args_top = args_bottom - args_size;
    args_top &= -16;

    /* argc */
    uintptr_t *args_p = (uintptr_t *) args_top;
    assert(memory_write(mem, (uintptr_t) args_p, (void *) &argc, sizeof(int)));
    args_p++;

    /* args */
    /* used for calculating the offset of args when pushing to stack */
    size_t args_space[256];
    size_t args_space_idx = 0;
    size_t args_len;
    size_t args_len_total = 0;
    for (int i = 0; i < argc; i++) {
        const char *arg = args[i];
        args_len = strlen(arg);
        assert(memory_write(mem, (uintptr_t) args_p, (void *) arg,
                            (args_len + 1) * sizeof(uint8_t)));
        args_space[args_space_idx++] = args_len + 1;
        args_p = (uintptr_t *) ((uintptr_t) args_p + args_len + 1);
        args_len_total += args_len + 1;
    }
    args_p = (uintptr_t *) ((uintptr_t) args_p - args_len_total);
    args_p--; /* point to argc */

    /* ready to push argc, args to stack */
    int stack_size = (1 + argc + 1) * sizeof(uint32_t);
    uintptr_t stack_bottom = (uintptr_t) rv->X[rv_reg_sp];
    uintptr_t stack_top = stack_bottom - stack_size;
    stack_top &= -16;

    /* argc */
    uintptr_t *sp = (uintptr_t *) stack_top;
    assert(memory_write(mem, (uintptr_t) sp,
                        (void *) (mem->mem_base + (uintptr_t) args_p),
                        sizeof(int)));
    args_p++;
    /* keep argc and args[0] within one word due to RV32 ABI */
    sp = (uintptr_t *) ((uint32_t *) sp + 1);

    /* args */
    for (int i = 0; i < argc; i++) {
        uintptr_t offset = (uintptr_t) args_p;
        assert(memory_write(mem, (uintptr_t) sp, (void *) &offset,
                            sizeof(uintptr_t)));
        args_p = (uintptr_t *) ((uintptr_t) args_p + args_space[i]);
        sp = (uintptr_t *) ((uint32_t *) sp + 1);
    }
    assert(memory_fill(mem, (uintptr_t) sp, sizeof(uint32_t), 0));

    /* reset sp pointing to argc */
    rv->X[rv_reg_sp] = stack_top;

    /* reset privilege mode */
#if RV32_HAS(SYSTEM)
    /*
     * System simulation defaults to S-mode as
     * it does not rely on M-mode software like OpenSBI.
     */
    rv->priv_mode = RV_PRIV_S_MODE;

    /* not being trapped */
    rv->is_trapped = false;

    /* Reset address translation: clear SATP and flush both TLBs to prevent
     * stale translations from previous execution.
     */
    rv->csr_satp = 0;
    memset(rv->dtlb, 0, sizeof(rv->dtlb));
    memset(rv->itlb, 0, sizeof(rv->itlb));
#else
    /* ISA simulation defaults to M-mode */
    rv->priv_mode = RV_PRIV_M_MODE;
#endif

    /* reset the csrs */
    rv->csr_mtvec = 0;
    rv->csr_cycle = 0;
#if RV32_HAS(SYSTEM)
    rv->timer_offset = 0;
#endif
    rv->csr_mstatus = 0;
    rv->csr_misa |= MISA_SUPER | MISA_USER;
    rv->csr_mvendorid = RV_MVENDORID;
    rv->csr_marchid = RV_MARCHID;
    rv->csr_mimpid = RV_MIMPID;
#if !RV32_HAS(RV32E)
    rv->csr_misa |= MISA_I;
#else
    rv->csr_misa |= MISA_E;
#endif
#if RV32_HAS(EXT_A)
    rv->csr_misa |= MISA_A;
#endif
#if RV32_HAS(EXT_C)
    rv->csr_misa |= MISA_C;
#endif
#if RV32_HAS(EXT_F)
    rv->csr_misa |= MISA_F;
    /* reset float registers */
    for (int i = 0; i < N_RV_REGS; i++)
        rv->F[i].v = 0;
    rv->csr_fcsr = 0;
#endif
#if RV32_HAS(EXT_M)
    rv->csr_misa |= MISA_M;
#endif

    rv->halt = false;
}

static const char *insn_name_table[] = {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = #inst,
    RV_INSN_LIST
#undef _
#define _(inst) [rv_insn_##inst] = #inst,
        FUSE_INSN_LIST
#undef _
};

#if RV32_HAS(JIT)
static void profile(block_t *block, uint32_t freq, FILE *output_file)
{
    fprintf(output_file, "%#-9x|", block->pc_start);
    fprintf(output_file, "%#-8x|", block->pc_end);
    fprintf(output_file, " %-10u|", freq);
    fprintf(output_file, " %-5s |", block->hot ? "true" : "false");
    fprintf(output_file, " %-6s |", block->has_loops ? "true" : "false");
    rv_insn_t *taken = block->ir_tail->branch_taken,
              *untaken = block->ir_tail->branch_untaken;
    if (untaken)
        fprintf(output_file, "%#-9x|", untaken->pc);
    else
        fprintf(output_file, "%-9s|", "NULL");
    if (taken)
        fprintf(output_file, "%#-8x|", taken->pc);
    else
        fprintf(output_file, "%-8s|", "NULL");
    rv_insn_t *ir = block->ir_head;
    while (1) {
        assert(ir);
        fprintf(output_file, "%s", insn_name_table[ir->opcode]);
        if (!ir->next)
            break;
        ir = ir->next;
        fprintf(output_file, " - ");
    }
    fprintf(output_file, "\n");
}
#endif

void rv_profile(riscv_t *rv, char *out_file_path)
{
    if (!out_file_path) {
        rv_log_warn("Profiling data output file is NULL");
        return;
    }
    FILE *f = fopen(out_file_path, "w");
    if (!f) {
        rv_log_error("Cannot open profiling data output file");
        return;
    }
#if RV32_HAS(JIT)
    fprintf(f,
            "PC start |PC end  | frequency |  hot  | loop  | untaken | taken | "
            "IR list \n");
    cache_profile(rv->block_cache, f, (prof_func_t) profile);
#else
    fprintf(f, "PC start |PC end  | untaken | taken  | IR list \n");
    block_map_t *map = &rv->block_map;
    for (uint32_t i = 0; i < map->block_capacity; i++) {
        block_t *block = map->map[i];
        if (!block)
            continue;
        fprintf(f, "%#-9x|", block->pc_start);
        fprintf(f, "%#-8x|", block->pc_end);
        rv_insn_t *taken = block->ir_tail->branch_taken,
                  *untaken = block->ir_tail->branch_untaken;
        if (untaken)
            fprintf(f, "%#-9x|", untaken->pc);
        else
            fprintf(f, "%-9s|", "NULL");
        if (taken)
            fprintf(f, "%#-8x|", taken->pc);
        else
            fprintf(f, "%-8s|", "NULL");
        rv_insn_t *ir = block->ir_head;
        while (1) {
            assert(ir);
            fprintf(f, "%s", insn_name_table[ir->opcode]);
            if (!ir->next)
                break;
            ir = ir->next;
            fprintf(f, " - ");
        }
        fprintf(f, "\n");
    }
#endif
}
