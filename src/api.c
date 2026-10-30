/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/rv32emu.h"
#include "riscv_private.h"
#if RV32_HAS(SDL)
#include "syscall_sdl.h"
#endif

struct rv32emu {
    riscv_t *cpu;
    vm_attr_t attr;
    bool started;
};

#define MAX_ARGS 128
#define MAX_ARG_BYTES 2048
#define MIN_MEMORY_SIZE 16384
#define MEMORY_ALIGN 4096

/* The memory backend and dispatcher currently have process-global state. */
static bool instance_live;

void rv32emu_options_init(rv32emu_options_t *options)
{
    if (options)
        *options = (rv32emu_options_t) {.memory_size = 64U << 20};
}

rv32emu_status_t rv32emu_create(const char *elf_path,
                                const rv32emu_options_t *options,
                                rv32emu_t **out)
{
    if (!out)
        return RV32EMU_INVALID_ARGUMENT;
    *out = NULL;
    if (!elf_path || !*elf_path)
        return RV32EMU_INVALID_ARGUMENT;
    rv32emu_options_t defaults;
    rv32emu_options_init(&defaults);
    if (!options)
        options = &defaults;
    /* The backend unmaps page-granular slack around the arena. */
    const long host_page = sysconf(_SC_PAGESIZE);
    const uint64_t align = host_page > MEMORY_ALIGN ? host_page : MEMORY_ALIGN;
    if (options->memory_size < MIN_MEMORY_SIZE ||
        options->memory_size % align ||
        options->memory_size > UINT64_C(0x100000000) || options->argc < 0 ||
        options->argc > MAX_ARGS || (options->argc && !options->argv))
        return RV32EMU_INVALID_ARGUMENT;
    char *args[MAX_ARGS];
    int argc = options->argc ? options->argc : 1;
    size_t bytes = 0;
    for (int i = 0; i < argc; i++) {
        const char *arg = options->argc ? options->argv[i] : elf_path;
        if (!arg)
            return RV32EMU_INVALID_ARGUMENT;
        size_t len = strlen(arg);
        if (len >= MAX_ARG_BYTES - bytes)
            return RV32EMU_INVALID_ARGUMENT;
        bytes += len + 1;
        args[i] = (char *) arg;
    }
    if (instance_live)
        return RV32EMU_BUSY;
    rv32emu_t *emu = calloc(1, sizeof(*emu));
    if (!emu)
        return RV32EMU_CREATE_FAILED;
    emu->attr = (vm_attr_t) {
        .mem_size = options->memory_size,
        .stack_size = MEMORY_ALIGN,
        .args_offset_size = MEMORY_ALIGN,
        .argc = argc,
        .argv = args,
        .log_level = LOG_ERROR,
        .cycle_per_step = 1000,
    };
    emu->attr.data.user.elf_program = (char *) elf_path;
    /* aot_collect and aot_library stay clear, so T2C never runs and every
     * native tier honors the bounded-step deadline.
     */
    emu->cpu = rv_create(&emu->attr);
    if (!emu->cpu) {
        free(emu);
        return RV32EMU_CREATE_FAILED;
    }
#if RV32_HAS(JIT)
    emu->cpu->jit_bounded_step = true;
    /* Native deadlines require counting every block in a chain. */
    emu->cpu->jit_entry_cycles = false;
#endif
    /* No public reboot/trace operation retains caller-owned strings. */
    emu->attr.argc = 0;
    emu->attr.argv = NULL;
    emu->attr.data.user.elf_program = NULL;
    instance_live = true;
    *out = emu;
    return RV32EMU_OK;
}

void rv32emu_destroy(rv32emu_t *emu)
{
    if (!emu)
        return;
    /* Guest-opened streams belong to the VM, standard streams to the host. */
    map_iter_t it;
    map_first(emu->attr.fd_map, &it);
    for (; !map_at_end(&it); map_next(&it)) {
        if (map_iter_key(&it, int) >= 3)
            fclose(map_iter_value(&it, FILE *));
    }
    rv_delete(emu->cpu);
#if RV32_HAS(SDL)
    /* SDL state is process-global; do not carry a guest's window or mixer
     * into the next instance, but leave a host's own SDL state alone.
     */
    sdl_video_audio_cleanup();
#endif
    free(emu);
    instance_live = false;
}

rv32emu_status_t rv32emu_step(rv32emu_t *emu, uint32_t budget)
{
    if (!emu || !budget || budget > INT_MAX)
        return RV32EMU_INVALID_ARGUMENT;
    if (rv_has_halted(emu->cpu))
        return RV32EMU_HALTED;
    emu->started = true;
    emu->attr.cycle_per_step = (int) budget;
    rv_step(emu->cpu);
    return rv_has_halted(emu->cpu) ? RV32EMU_HALTED : RV32EMU_OK;
}

rv32emu_status_t rv32emu_halt(rv32emu_t *emu)
{
    if (!emu)
        return RV32EMU_INVALID_ARGUMENT;
    rv_halt(emu->cpu);
    return RV32EMU_OK;
}

rv32emu_status_t rv32emu_exit_code(const rv32emu_t *emu, int *exit_code)
{
    if (!emu || !exit_code)
        return RV32EMU_INVALID_ARGUMENT;
    if (!rv_has_halted(emu->cpu))
        return RV32EMU_INVALID_STATE;
    *exit_code = emu->attr.exit_code;
    return RV32EMU_OK;
}

rv32emu_status_t rv32emu_get_reg(const rv32emu_t *emu,
                                 unsigned reg,
                                 uint32_t *value)
{
    if (!emu || !value || reg >= N_RV_REGS)
        return RV32EMU_INVALID_ARGUMENT;
    *value = rv_get_reg(emu->cpu, reg);
    return RV32EMU_OK;
}

rv32emu_status_t rv32emu_set_reg(rv32emu_t *emu, unsigned reg, uint32_t value)
{
    if (!emu || reg >= N_RV_REGS)
        return RV32EMU_INVALID_ARGUMENT;
    rv_set_reg(emu->cpu, reg, value);
    return RV32EMU_OK;
}

rv32emu_status_t rv32emu_get_pc(const rv32emu_t *emu, uint32_t *value)
{
    if (!emu || !value)
        return RV32EMU_INVALID_ARGUMENT;
    *value = rv_get_pc(emu->cpu);
    return RV32EMU_OK;
}

rv32emu_status_t rv32emu_set_pc(rv32emu_t *emu, uint32_t value)
{
    if (!emu || !rv_set_pc(emu->cpu, value))
        return RV32EMU_INVALID_ARGUMENT;
    rv_reset_dispatcher_state();
    return RV32EMU_OK;
}

/* A null buffer is acceptable only for an empty range. */
static bool valid_access(const rv32emu_t *emu,
                         uint32_t address,
                         const void *data,
                         size_t size)
{
    return emu && (data || !size) && address <= emu->attr.mem->mem_size &&
           size <= emu->attr.mem->mem_size - address;
}

rv32emu_status_t rv32emu_read_memory(const rv32emu_t *emu,
                                     uint32_t address,
                                     void *data,
                                     size_t size)
{
    if (!valid_access(emu, address, data, size))
        return RV32EMU_INVALID_ARGUMENT;
    if (size)
        memcpy(data, emu->attr.mem->mem_base + address, size);
    return RV32EMU_OK;
}

rv32emu_status_t rv32emu_write_memory(rv32emu_t *emu,
                                      uint32_t address,
                                      const void *data,
                                      size_t size)
{
    if (!valid_access(emu, address, data, size))
        return RV32EMU_INVALID_ARGUMENT;
    if (emu->started)
        return RV32EMU_INVALID_STATE;
    if (size) {
        memcpy(emu->attr.mem->mem_base + address, data, size);
#if RV32_HAS(JIT)
        /* ELF analysis no longer describes potentially patched code. */
        emu->cpu->jit_elide_align_checks = false;
#endif
    }
    return RV32EMU_OK;
}

const char *rv32emu_status_string(rv32emu_status_t status)
{
    switch (status) {
    case RV32EMU_OK:
        return "success";
    case RV32EMU_HALTED:
        return "halted";
    case RV32EMU_INVALID_ARGUMENT:
        return "invalid argument";
    case RV32EMU_BUSY:
        return "an instance is already active";
    case RV32EMU_CREATE_FAILED:
        return "unable to allocate or load emulator";
    case RV32EMU_INVALID_STATE:
        return "operation unavailable in this state";
    default:
        return "unknown status";
    }
}
