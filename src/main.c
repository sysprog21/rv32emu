#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "state.h"

/* enable program trace mode */
static bool opt_trace = false;
#if RV32_HAS(GDBSTUB)
/* enable program gdbstub mode */
static bool opt_gdbstub = false;
#endif

/* RISCV arch-test */
static bool opt_arch_test = false;
static char *signature_out_file;

/* target executable */
static const char *opt_prog_name = "a.out";

#define MEMIO(op) on_mem_##op
#define IO_HANDLER_IMPL(type, op, RW)                                  \
    static IIF(RW)(                                                    \
        /* W */ void MEMIO(op)(struct riscv_t * rv, riscv_word_t addr, \
                               riscv_##type##_t data),                 \
        /* R */ riscv_##type##_t MEMIO(op)(struct riscv_t * rv,        \
                                           riscv_word_t addr))         \
    {                                                                  \
        state_t *s = rv_userdata(rv);                                  \
        IIF(RW)                                                        \
        (memory_write(s->mem, addr, (uint8_t *) &data, sizeof(data)),  \
         return memory_##op(s->mem, addr));                            \
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
static void run_and_trace(struct riscv_t *rv, elf_t *elf)
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

static void run(struct riscv_t *rv)
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
            "Usage: %s [options] [filename]\n"
            "Options:\n"
            "  --trace : print executable trace\n"
#if RV32_HAS(GDBSTUB)
            "  --gdbstub : allow remote GDB connections (as gdbstub)\n"
#endif
            "  --arch-test [filename] : dump signature to the given file, "
            "required by arch-test test\n",
            filename);
}

static bool parse_args(int argc, char **args)
{
    /* parse each argument in turn */
    for (int i = 1; i < argc; ++i) {
        const char *arg = args[i];
        /* parse flags */
        if (arg[0] == '-') {
            if (!strcmp(arg, "--help"))
                return false;
            if (!strcmp(arg, "--trace")) {
                opt_trace = true;
                continue;
            }
#if RV32_HAS(GDBSTUB)
            if (!strcmp(arg, "--gdbstub")) {
                opt_gdbstub = true;
                continue;
            }
#endif
            if (!strcmp(arg, "--arch-test")) {
                opt_arch_test = true;
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "Filename for signature output required by "
                            "arch-test.\n");
                    return false;
                }
                signature_out_file = args[++i];
                continue;
            }
            /* otherwise, error */
            fprintf(stderr, "Unknown argument '%s'\n", arg);
            return false;
        }
        /* set the executable */
        opt_prog_name = arg;
    }

    return true;
}

static void dump_test_signature(struct riscv_t *rv, elf_t *elf)
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

    state_t *s = rv_userdata(rv);

    /* dump it word by word */
    for (uint32_t addr = start; addr < end; addr += 4)
        fprintf(f, "%08x\n", memory_read_w(s->mem, addr));

    fclose(f);
}

int main(int argc, char **args)
{
    if (!parse_args(argc, args)) {
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
    const struct riscv_io_t io = {
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
        .on_ecall = syscall_handler,
        .on_ebreak = ebreak_handler,
    };

    state_t *state = state_new();

    /* find the start of the heap */
    const struct Elf32_Sym *end;
    if ((end = elf_get_symbol(elf, "_end")))
        state->break_addr = end->st_value;

    /* create the RISC-V runtime */
    struct riscv_t *rv = rv_create(&io, state);
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

    /* dump test result in test mode */
    if (opt_arch_test)
        dump_test_signature(rv, elf);

    /* finalize the RISC-V runtime */
    elf_delete(elf);
    rv_delete(rv);
    state_delete(state);

    return 0;
}
