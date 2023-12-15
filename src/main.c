/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elf.h"
#include "state.h"
#include "utils.h"

/* enable program trace mode */
static bool opt_trace = false;

#if RV32_HAS(GDBSTUB)
/* enable program gdbstub mode */
static bool opt_gdbstub = false;
#endif

/* dump registers as JSON */
static bool opt_dump_regs = false;
static char *registers_out_file;

/* RISC-V arch-test */
static bool opt_arch_test = false;
static char *signature_out_file;

/* Quiet outputs */
static bool opt_quiet_outputs = false;

/* target executable */
static const char *opt_prog_name = "a.out";

/* target argc and argv */
static int prog_argc;
static char **prog_args;
static const char *optstr = "tgqmhd:a:";

/* enable misaligned memory access */
static bool opt_misaligned = false;

#define MEMIO(op) on_mem_##op
#define IO_HANDLER_IMPL(type, op, RW)                                     \
    static IIF(RW)(                                                       \
        /* W */ void MEMIO(op)(riscv_word_t addr, riscv_##type##_t data), \
        /* R */ riscv_##type##_t MEMIO(op)(riscv_word_t addr))            \
    {                                                                     \
        IIF(RW)                                                           \
        (memory_##op(addr, (uint8_t *) &data), return memory_##op(addr)); \
    }

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

/* run: printing out an instruction trace */
static void run_and_trace(riscv_t *rv, elf_t *elf)
{
    const uint32_t cycles_per_step = 1;

    for (; !rv_has_halted(rv);) { /* run until the flag is done */
        /* trace execution */
        uint32_t pc = rv_get_pc(rv);
        const char *sym = elf_find_symbol(elf, pc);
        printf("%08x  %s\n", pc, (sym ? sym : ""));

        /* step instructions */
        rv_step(rv, cycles_per_step);
    }
}

static void run(riscv_t *rv)
{
    const uint32_t cycles_per_step = 100;
    for (; !rv_has_halted(rv);) { /* run until the flag is done */
        /* step instructions */
        rv_step(rv, cycles_per_step);
    }
}

static void print_usage(const char *filename)
{
    fprintf(stderr,
            "RV32I[MA] Emulator which loads an ELF file to execute.\n"
            "Usage: %s [options] [filename] [arguments]\n"
            "Options:\n"
            "  -t : print executable trace\n"
#if RV32_HAS(GDBSTUB)
            "  -g : allow remote GDB connections (as gdbstub)\n"
#endif
            "  -d [filename]: dump registers as JSON to the "
            "given file or `-` (STDOUT)\n"
            "  -q : Suppress outputs other than `dump-registers`\n"
            "  -a [filename] : dump signature to the given file, "
            "required by arch-test test\n"
            "  -m : enable misaligned memory access\n"
            "  -h : show this message\n",
            filename);
}

static bool parse_args(int argc, char **args)
{
    int opt;
    int emu_argc = 0;

    while ((opt = getopt(argc, args, optstr)) != -1) {
        emu_argc++;

        switch (opt) {
        case 't':
            opt_trace = true;
            break;
#if RV32_HAS(GDBSTUB)
        case 'g':
            opt_gdbstub = true;
            break;
#endif
        case 'q':
            opt_quiet_outputs = true;
            break;
        case 'h':
            return false;
        case 'm':
            opt_misaligned = true;
            break;
        case 'd':
            opt_dump_regs = true;
            registers_out_file = optarg;
            emu_argc++;
            break;
        case 'a':
            opt_arch_test = true;
            signature_out_file = optarg;
            emu_argc++;
            break;
        default:
            return false;
        }
    }

    prog_argc = argc - emu_argc - 1;
    /* optind points to the first non-option string, so it should indicate the
     * target program.
     */
    prog_args = &args[optind];
    opt_prog_name = prog_args[0];
    return true;
}

static void dump_test_signature(elf_t *elf)
{
    uint32_t start = 0, end = 0;
    const struct Elf32_Sym *sym;
    FILE *f = fopen(signature_out_file, "w");
    if (!f) {
        fprintf(stderr, "Cannot open signature output file.\n");
        return;
    }

    /* use the entire .data section as a fallback */
    elf_get_data_section_range(elf, &start, &end);

    /* try and access the exact signature range */
    if ((sym = elf_get_symbol(elf, "begin_signature")))
        start = sym->st_value;
    if ((sym = elf_get_symbol(elf, "end_signature")))
        end = sym->st_value;

    /* dump it word by word */
    for (uint32_t addr = start; addr < end; addr += 4)
        fprintf(f, "%08x\n", memory_read_w(addr));

    fclose(f);
}

int main(int argc, char **args)
{
    if (argc == 1 || !parse_args(argc, args)) {
        print_usage(args[0]);
        return 1;
    }

    /* open the ELF file from the file system */
    elf_t *elf = elf_new();
    if (!elf_open(elf, opt_prog_name)) {
        fprintf(stderr, "Unable to open ELF file '%s'\n", opt_prog_name);
        return 1;
    }

    /* install the I/O handlers for the RISC-V runtime */
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

        /* system */
        .on_ecall = ecall_handler,
        .on_ebreak = ebreak_handler,
        .on_memcpy = memcpy_handler,
        .on_memset = memset_handler,
        .allow_misalign = opt_misaligned,
    };

    state_t *state = state_new();

    /* find the start of the heap */
    const struct Elf32_Sym *end;
    if ((end = elf_get_symbol(elf, "_end")))
        state->break_addr = end->st_value;

    /* create the RISC-V runtime */
    riscv_t *rv =
        rv_create(&io, state, prog_argc, prog_args, !opt_quiet_outputs);
    if (!rv) {
        fprintf(stderr, "Unable to create riscv emulator\n");
        return 1;
    }

    /* load the ELF file into the memory abstraction */
    if (!elf_load(elf, rv, state->mem)) {
        fprintf(stderr, "Unable to load ELF file '%s'\n", args[1]);
        return 1;
    }

    /* run based on the specified mode */
    if (opt_trace) {
        run_and_trace(rv, elf);
    }
#if RV32_HAS(GDBSTUB)
    else if (opt_gdbstub) {
        rv_debug(rv);
    }
#endif
    else {
        run(rv);
    }

    /* dump registers as JSON */
    if (opt_dump_regs)
        dump_registers(rv, registers_out_file);

    /* dump test result in test mode */
    if (opt_arch_test)
        dump_test_signature(elf);

    /* finalize the RISC-V runtime */
    elf_delete(elf);
    rv_delete(rv);
    state_delete(state);

    return 0;
}
