/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elf.h"
#include "riscv.h"
#include "utils.h"

/* enable program trace mode */
#if !RV32_HAS(SYSTEM) || (RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER))
static bool opt_trace = false;
#endif

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
static char *opt_prog_name;

/* target argc and argv */
static int prog_argc;
static char **prog_args;
static const char *optstr = "tgqmhpd:a:k:i:b:";

/* enable misaligned memory access */
static bool opt_misaligned = false;

/* dump profiling data */
static bool opt_prof_data = false;
static char *prof_out_file;

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
/* Linux kernel data */
static char *opt_kernel_img;
static char *opt_rootfs_img;
static char *opt_dtb;
#endif

static void print_usage(const char *filename)
{
    fprintf(stderr,
            "RV32I[MAFC] Emulator which loads an ELF file to execute.\n"
            "Usage: %s [options] [filename] [arguments]\n"
            "Options:\n"
#if !RV32_HAS(SYSTEM) || (RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER))
            "  -t : print executable trace\n"
#endif
#if RV32_HAS(GDBSTUB)
            "  -g : allow remote GDB connections (as gdbstub)\n"
#endif
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
            "  -k <image> : use <image> as kernel image\n"
            "  -i <image> : use <image> as rootfs\n"
            "  -b <dtb> : use <dtb> as device tree blob\n"
#endif
            "  -d [filename]: dump registers as JSON to the "
            "given file or `-` (STDOUT)\n"
            "  -q : Suppress outputs other than `dump-registers`\n"
            "  -a [filename] : dump signature to the given file, "
            "required by arch-test test\n"
            "  -m : enable misaligned memory access\n"
            "  -p : generate profiling data\n"
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
#if !RV32_HAS(SYSTEM) || (RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER))
        case 't':
            opt_trace = true;
            break;
#endif
#if RV32_HAS(GDBSTUB)
        case 'g':
            opt_gdbstub = true;
            break;
#endif
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
        case 'k':
            opt_kernel_img = optarg;
            emu_argc++;
            break;
        case 'i':
            opt_rootfs_img = optarg;
            emu_argc++;
            break;
        case 'b':
            opt_dtb = optarg;
            emu_argc++;
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
        case 'p':
            opt_prof_data = true;
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

    if (opt_prof_data) {
        char cwd_path[PATH_MAX] = {0};
        assert(getcwd(cwd_path, PATH_MAX));

        char rel_path[PATH_MAX] = {0};
        memcpy(rel_path, args[0], strlen(args[0]) - 7 /* strlen("rv32emu") */);

        char *prog_basename = basename(opt_prog_name);
        prof_out_file = malloc(strlen(cwd_path) + 1 + strlen(rel_path) +
                               strlen(prog_basename) + 5 + 1);
        assert(prof_out_file);

        sprintf(prof_out_file, "%s/%s%s.prof", cwd_path, rel_path,
                prog_basename);
    }
    return true;
}

static void dump_test_signature(const char *prog_name)
{
    elf_t *elf = elf_new();
    assert(elf && elf_open(elf, prog_name));

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
    elf_delete(elf);
}

/* CYCLE_PER_STEP shall be defined on different runtime */
#ifndef CYCLE_PER_STEP
#define CYCLE_PER_STEP 100
#endif
/* FIXME: MEM_SIZE shall be defined on different runtime */
#ifndef MEM_SIZE
/* Allocate 2^{19} bytes, which is ample for all selective benchmarks. */
#define MEM_SIZE 0x80000ULL
#endif
#define STACK_SIZE 0x1000       /* 4096 */
#define ARGS_OFFSET_SIZE 0x1000 /* 4096 */

/* To use rv_halt function in wasm, we have to expose RISC-V instance(rv),
 * but we can add a layer to not expose the instance and make rv_halt
 * callable. A small trade-off is that declaring instance as a global
 * variable. rv_halt is useful when cancelling the main loop of wasm,
 * see rv_step in emulate.c for more detail
 */
riscv_t *rv;
#ifdef __EMSCRIPTEN__
void indirect_rv_halt()
{
    rv_halt(rv);
}
#endif

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
/* forcely undefine MEM_SIZE to prevent any define in Makefile */
#undef MEM_SIZE
#define MEM_SIZE 512 * 1024 * 1024
#endif

int main(int argc, char **args)
{
    if (argc == 1 || !parse_args(argc, args)) {
        print_usage(args[0]);
        return 1;
    }

    int run_flag = 0;
#if !RV32_HAS(SYSTEM) || (RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER))
    run_flag |= opt_trace;
#endif
#if RV32_HAS(GDBSTUB)
    run_flag |= opt_gdbstub << 1;
#endif
    run_flag |= opt_prof_data << 2;

    vm_attr_t attr = {
        .mem_size = MEM_SIZE,
        .stack_size = STACK_SIZE,
        .args_offset_size = ARGS_OFFSET_SIZE,
        .argc = prog_argc,
        .argv = prog_args,
        .log_level = 0,
        .run_flag = run_flag,
        .profile_output_file = prof_out_file,
        .cycle_per_step = CYCLE_PER_STEP,
        .allow_misalign = opt_misaligned,
    };
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    attr.data.system.kernel = opt_kernel_img;
    attr.data.system.initrd = opt_rootfs_img;
    attr.data.system.dtb = opt_dtb;
#else
    attr.data.user.elf_program = opt_prog_name;
#endif

    /* create the RISC-V runtime */
    rv = rv_create(&attr);
    if (!rv) {
        fprintf(stderr, "Unable to create riscv emulator\n");
        attr.exit_code = 1;
        goto end;
    }

    rv_run(rv);

    /* dump registers as JSON */
    if (opt_dump_regs)
        dump_registers(rv, registers_out_file);

    /* dump test result in test mode */
    if (opt_arch_test)
        dump_test_signature(opt_prog_name);

    /* finalize the RISC-V runtime */
    rv_delete(rv);

    printf("inferior exit code %d\n", attr.exit_code);

end:
    free(prof_out_file);
    return attr.exit_code;
}
