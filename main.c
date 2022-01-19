#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "state.h"

/* enable program trace mode */
static bool opt_trace = false;

/* RISCV compliance test mode */
static bool opt_compliance = false;
static char *signature_out_file;

/* target executable */
static const char *opt_prog_name = "a.out";

static riscv_word_t on_mem_ifetch(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = rv_userdata(rv);
    return memory_read_ifetch(s->mem, addr);
}

static riscv_word_t on_mem_read_w(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = rv_userdata(rv);
    return memory_read_w(s->mem, addr);
}

static riscv_half_t on_mem_read_s(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = rv_userdata(rv);
    return memory_read_s(s->mem, addr);
}

static riscv_byte_t on_mem_read_b(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = rv_userdata(rv);
    return memory_read_b(s->mem, addr);
}

static void on_mem_write_w(struct riscv_t *rv,
                           riscv_word_t addr,
                           riscv_word_t data)
{
    state_t *s = rv_userdata(rv);
    memory_write(s->mem, addr, (uint8_t *) &data, sizeof(data));
}

static void on_mem_write_s(struct riscv_t *rv,
                           riscv_word_t addr,
                           riscv_half_t data)
{
    state_t *s = rv_userdata(rv);
    memory_write(s->mem, addr, (uint8_t *) &data, sizeof(data));
}

static void on_mem_write_b(struct riscv_t *rv,
                           riscv_word_t addr,
                           riscv_byte_t data)
{
    state_t *s = rv_userdata(rv);
    memory_write(s->mem, addr, (uint8_t *) &data, sizeof(data));
}

extern void syscall_handler(struct riscv_t *);
static void on_on_ecall(struct riscv_t *rv)
{
    syscall_handler(rv);
}

static void on_on_ebreak(struct riscv_t *rv)
{
    rv_halt(rv);
}

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
            "  --compliance [filename] : dump signature to the given file, required by compliance test\n",
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
            if (!strcmp(arg, "--compliance")) {
                opt_compliance = true;
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "Filename for signature output required in compliance mode.\n");
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

void dump_test_signature(struct riscv_t *rv, elf_t *elf)
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
    for (uint32_t addr = start; addr < end; addr += 4) {
        fprintf(f, "%08x\n", memory_read_w(s->mem, addr));
    }

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
        on_mem_ifetch,  on_mem_read_w,  on_mem_read_s,
        on_mem_read_b,  on_mem_write_w, on_mem_write_s,
        on_mem_write_b, on_on_ecall,    on_on_ebreak,
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
    } else {
        run(rv);
    }

    /* dump test result in test mode */
    if (opt_compliance) {
        dump_test_signature(rv, elf);
    }

    /* finalize the RISC-V runtime */
    elf_delete(elf);
    rv_delete(rv);
    state_delete(state);

    return 0;
}
