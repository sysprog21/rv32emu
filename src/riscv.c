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

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
#include <termios.h>
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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
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
            free(ir->fuse);
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
        else if (fd == STDOUT_FILENO)
            attr->fd_stdout = new_fd;
        else
            attr->fd_stderr = new_fd;
    }
}

#define MEMIO(op) on_mem_##op
#define IO_HANDLER_IMPL(type, op, RW)                                     \
    static IIF(RW)(                                                       \
        /* W */ void MEMIO(op)(UNUSED riscv_t * rv, riscv_word_t addr,    \
                               riscv_##type##_t data),                    \
        /* R */ riscv_##type##_t MEMIO(op)(UNUSED riscv_t * rv,           \
                                           riscv_word_t addr))            \
    {                                                                     \
        IIF(RW)                                                           \
        (memory_##op(addr, (uint8_t *) &data), return memory_##op(addr)); \
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
    while (!rv->quit) {
        if (!list_empty(&rv->wait_queue)) {
            queue_entry_t *entry =
                list_last_entry(&rv->wait_queue, queue_entry_t, list);
            pthread_mutex_lock(&rv->wait_queue_lock);
            list_del_init(&entry->list);
            pthread_mutex_unlock(&rv->wait_queue_lock);
            pthread_mutex_lock(&rv->cache_lock);
            t2c_compile(rv, entry->block);
            pthread_mutex_unlock(&rv->cache_lock);
            free(entry);
        }
    }
    return NULL;
}
#endif

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
static void map_file(char **ram_loc, const char *name)
{
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        goto fail;

    /* get file size */
    struct stat st;
    fstat(fd, &st);

#if HAVE_MMAP
    /* remap to a memory region */
    *ram_loc = mmap(*ram_loc, st.st_size, PROT_READ | PROT_WRITE,
                    MAP_FIXED | MAP_PRIVATE, fd, 0);
    if (*ram_loc == MAP_FAILED)
        goto cleanup;
#else
    /* calloc and load data to a memory region */
    *ram_loc = calloc(st.st_size, sizeof(uint8_t));
    if (!*ram_loc)
        goto cleanup;
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
    return;

cleanup:
    close(fd);
fail:
    fprintf(stderr, "Error: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
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

riscv_t *rv_create(riscv_user_t rv_attr)
{
    assert(rv_attr);

    riscv_t *rv = calloc(1, sizeof(riscv_t));
    assert(rv);

    /* register cleaning callback for CTRL+a+x exit */
    atexit(rv_async_block_clear);

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
     */
    attr->fd_map = map_init(int, FILE *, map_cmp_int);
    rv_remap_stdstream(rv,
                       (fd_stream_pair_t[]){
                           {STDIN_FILENO, stdin},
                           {STDOUT_FILENO, stdout},
                           {STDERR_FILENO, stderr},
                       },
                       3);

#if !RV32_HAS(SYSTEM) || (RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER))
    elf_t *elf = elf_new();
    assert(elf && elf_open(elf, attr->data.user.elf_program));

    const struct Elf32_Sym *end;
    if ((end = elf_get_symbol(elf, "_end")))
        attr->break_addr = end->st_value;

    assert(elf_load(elf, attr->mem));

    /* set the entry pc */
    const struct Elf32_Ehdr *hdr = get_elf_header(elf);
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

    char *ram_loc = (char *) attr->mem->mem_base;
    map_file(&ram_loc, attr->data.system.kernel);

    uint32_t dtb_addr = attr->mem->mem_size - (1 * 1024 * 1024);
    ram_loc = ((char *) attr->mem->mem_base) + dtb_addr;
    map_file(&ram_loc, attr->data.system.dtb);
    /*
     * Load optional initrd image at last 8 MiB before the dtb region to
     * prevent kernel from overwritting it
     */
    if (attr->data.system.initrd) {
        uint32_t initrd_addr = dtb_addr - (8 * 1024 * 1024);
        ram_loc = ((char *) attr->mem->mem_base) + initrd_addr;
        map_file(&ram_loc, attr->data.system.initrd);
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

    capture_keyboard_input();
#endif /* !RV32_HAS(SYSTEM) || (RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER)) */

    /* create block and IRs memory pool */
    rv->block_mp = mpool_create(sizeof(block_t) << BLOCK_MAP_CAPACITY_BITS,
                                sizeof(block_t));
    rv->block_ir_mp = mpool_create(
        sizeof(rv_insn_t) << BLOCK_IR_MAP_CAPACITY_BITS, sizeof(rv_insn_t));

#if !RV32_HAS(JIT)
    /* initialize the block map */
    block_map_init(&rv->block_map, BLOCK_MAP_CAPACITY_BITS);
#else
    INIT_LIST_HEAD(&rv->block_list);
    rv->jit_state = jit_state_init(CODE_CACHE_SIZE);
    rv->block_cache = cache_create(BLOCK_MAP_CAPACITY_BITS);
    assert(rv->block_cache);
#if RV32_HAS(T2C)
    rv->quit = false;
    rv->jit_cache = jit_cache_init();
    /* prepare wait queue. */
    pthread_mutex_init(&rv->wait_queue_lock, NULL);
    pthread_mutex_init(&rv->cache_lock, NULL);
    INIT_LIST_HEAD(&rv->wait_queue);
    /* activate the background compilation thread. */
    pthread_create(&t2c_thread, NULL, t2c_runloop, rv);
#endif
#endif

    return rv;
}

#if !RV32_HAS(SYSTEM) || (RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER))
/*
 * TODO: enable to trace Linux kernel symbol
 */
static void rv_run_and_trace(riscv_t *rv)
{
    assert(rv);

    vm_attr_t *attr = PRIV(rv);
    assert(attr && attr->data.user.elf_program);
    attr->cycle_per_step = 1;

    const char *prog_name = attr->data.user.elf_program;
    elf_t *elf = elf_new();
    assert(elf && elf_open(elf, prog_name));

    for (; !rv_has_halted(rv);) { /* run until the flag is done */
        /* trace execution */
        uint32_t pc = rv_get_pc(rv);
        const char *sym = elf_find_symbol(elf, pc);
        printf("%08x  %s\n", pc, (sym ? sym : ""));

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
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
           attr->data.system.kernel && attr->data.system.initrd &&
           attr->data.system.dtb
#else
           attr->data.user.elf_program
#endif
    );
    attr->cycle_per_step = 100000000;

    if (!(attr->run_flag & (RV_RUN_TRACE | RV_RUN_GDBSTUB))) {
#ifdef __EMSCRIPTEN__
        emscripten_set_main_loop_arg(rv_step, (void *) rv, 0, 1);
#else
        /* default main loop */
        for (; !rv_has_halted(rv);) /* run until the flag is done */
            rv_step(rv);            /* step instructions */
#endif
    }
#if !RV32_HAS(SYSTEM) || (RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER))
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

void rv_delete(riscv_t *rv)
{
    assert(rv);
#if !RV32_HAS(JIT)
    vm_attr_t *attr = PRIV(rv);
    map_delete(attr->fd_map);
    memory_delete(attr->mem);
    block_map_destroy(rv);
#else
#if RV32_HAS(T2C)
    rv->quit = true;
    pthread_join(t2c_thread, NULL);
    pthread_mutex_destroy(&rv->wait_queue_lock);
    pthread_mutex_destroy(&rv->cache_lock);
    jit_cache_exit(rv->jit_cache);
#endif
    jit_state_exit(rv->jit_state);
    cache_free(rv->block_cache);
#endif
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    u8250_delete(attr->uart);
    plic_delete(attr->plic);
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
    args_top &= 16;

    /* argc */
    uintptr_t *args_p = (uintptr_t *) args_top;
    memory_write(mem, (uintptr_t) args_p, (void *) &argc, sizeof(int));
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
        memory_write(mem, (uintptr_t) args_p, (void *) arg,
                     (args_len + 1) * sizeof(uint8_t));
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
    memory_write(mem, (uintptr_t) sp,
                 (void *) (mem->mem_base + (uintptr_t) args_p), sizeof(int));
    args_p++;
    /* keep argc and args[0] within one word due to RV32 ABI */
    sp = (uintptr_t *) ((uint32_t *) sp + 1);

    /* args */
    for (int i = 0; i < argc; i++) {
        uintptr_t offset = (uintptr_t) args_p;
        memory_write(mem, (uintptr_t) sp, (void *) &offset, sizeof(uintptr_t));
        args_p = (uintptr_t *) ((uintptr_t) args_p + args_space[i]);
        sp = (uintptr_t *) ((uint32_t *) sp + 1);
    }
    memory_fill(mem, (uintptr_t) sp, sizeof(uint32_t), 0);

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
#else
    /* ISA simulation defaults to M-mode */
    rv->priv_mode = RV_PRIV_M_MODE;
#endif

    /* reset the csrs */
    rv->csr_mtvec = 0;
    rv->csr_cycle = 0;
    rv->csr_mstatus = 0;
    rv->csr_misa |= MISA_SUPER | MISA_USER | MISA_I;
    rv->csr_mvendorid = RV_MVENDORID;
    rv->csr_marchid = RV_MARCHID;
    rv->csr_mimpid = RV_MIMPID;
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
        fprintf(stderr, "Profiling data output file is NULL.\n");
        return;
    }
    FILE *f = fopen(out_file_path, "w");
    if (!f) {
        fprintf(stderr, "Cannot open profiling data output file.\n");
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
