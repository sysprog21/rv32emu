#include <cstring>
#include <ctime>
#include <memory>

#include "elf.h"
#include "io.h"

#include "riscv.h"
#include "state.h"

// enable program trace mode
static bool g_arg_trace = false;

// target executable
static const char *g_arg_program = "a.out";

static riscv_word_t imp_mem_ifetch(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = reinterpret_cast<state_t *>(rv_userdata(rv));
    return s->mem.read_ifetch(addr);
}

static riscv_word_t imp_mem_read_w(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = reinterpret_cast<state_t *>(rv_userdata(rv));
    return s->mem.read_w(addr);
}

static riscv_half_t imp_mem_read_s(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = reinterpret_cast<state_t *>(rv_userdata(rv));
    return s->mem.read_s(addr);
}

static riscv_byte_t imp_mem_read_b(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = reinterpret_cast<state_t *>(rv_userdata(rv));
    return s->mem.read_b(addr);
}

static void imp_mem_write_w(struct riscv_t *rv,
                            riscv_word_t addr,
                            riscv_word_t data)
{
    state_t *s = reinterpret_cast<state_t *>(rv_userdata(rv));
    s->mem.write(addr, (uint8_t *) &data, sizeof(data));
}

static void imp_mem_write_s(struct riscv_t *rv,
                            riscv_word_t addr,
                            riscv_half_t data)
{
    state_t *s = reinterpret_cast<state_t *>(rv_userdata(rv));
    s->mem.write(addr, (uint8_t *) &data, sizeof(data));
}

static void imp_mem_write_b(struct riscv_t *rv,
                            riscv_word_t addr,
                            riscv_byte_t data)
{
    state_t *s = reinterpret_cast<state_t *>(rv_userdata(rv));
    s->mem.write(addr, (uint8_t *) &data, sizeof(data));
}

static void imp_on_ecall(struct riscv_t *rv)
{
    // pass to the syscall handler
    syscall_handler(rv);
}

static void imp_on_ebreak(struct riscv_t *rv)
{
    rv_halt(rv);
}

// run the core - printing out an instruction trace
static void run_and_trace(riscv_t *rv, state_t *state, elf_t &elf)
{
    const uint32_t cycles_per_step = 1;

    // run until we see the flag that we are done
    for (; !rv_has_halted(rv);) {
        // trace execution
        uint32_t pc = rv_get_pc(rv);
        const char *sym = elf.find_symbol(pc);
        printf("%08x  %s\n", pc, (sym ? sym : ""));

        // step instructions
        rv_step(rv, cycles_per_step);
    }
}

// run the core
static void run(riscv_t *rv, state_t *state, elf_t &elf)
{
    const uint32_t cycles_per_step = 100;
    // run until we see the flag that we are done
    for (; !rv_has_halted(rv);) {
        // step instructions
        rv_step(rv, cycles_per_step);
    }
}

static void print_usage(const char *filename)
{
    fprintf(stderr, R"(
RV32I[MA] Emulator which loads an ELF file to execute.
Usage: %s [options] [filename]
Options:
  --trace : print executable trace
)",
            filename);
}

static bool parse_args(int argc, char **args)
{
    // parse each argument in turn
    for (int i = 1; i < argc; ++i) {
        const char *arg = args[i];
        // parse flags
        if (arg[0] == '-') {
            if (!strcmp(arg, "--help"))
                return false;
            if (!strcmp(arg, "--trace")) {
                g_arg_trace = true;
                continue;
            }
            // otherwise, error
            fprintf(stderr, "Unknown argument '%s'\n", arg);
            return false;
        }
        // set the executable
        g_arg_program = arg;
    }

    return true;
}

int main(int argc, char **args)
{
    // parse the program arguments
    if (!parse_args(argc, args)) {
        print_usage(args[0]);
        return 1;
    }

    // open the ELF file from disk
    elf_t elf;
    if (!elf.open(g_arg_program)) {
        fprintf(stderr, "Unable to open ELF file '%s'\n", g_arg_program);
        return 1;
    }

    // setup the IO handlers for the VM
    const riscv_io_t io = {
        imp_mem_ifetch,  imp_mem_read_w,  imp_mem_read_s,
        imp_mem_read_b,  imp_mem_write_w, imp_mem_write_s,
        imp_mem_write_b, imp_on_ecall,    imp_on_ebreak,
    };

    auto state = std::make_unique<state_t>();
    state->break_addr = 0;
    state->fd_map[0] = stdin;
    state->fd_map[1] = stdout;
    state->fd_map[2] = stderr;

    // find the start of the heap
    if (const ELF::Elf32_Sym *end = elf.get_symbol("_end"))
        state->break_addr = end->st_value;

    // create the VM
    riscv_t *rv = rv_create(&io, state.get());
    if (!rv) {
        fprintf(stderr, "Unable to create riscv emulator\n");
        return 1;
    }

    // load the ELF file into our memory abstraction
    if (!elf.load(rv, state->mem)) {
        fprintf(stderr, "Unable to load ELF file '%s'\n", args[1]);
        return 1;
    }

    // run based on the specified mode
    if (g_arg_trace) {
        run_and_trace(rv, state.get(), elf);
    } else {
        run(rv, state.get(), elf);
    }

    // delete the VM
    rv_delete(rv);
    return 0;
}
