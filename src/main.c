/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__EMSCRIPTEN__)
#include "em_runtime.h"
#endif

#include "elf.h"
#include "io.h"
#include "riscv.h"
#include "utils.h"

/* enable program trace mode */
#if !RV32_HAS(SYSTEM_MMIO)
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
static const char *optstr = "tgqmhpd:a:k:i:b:x:";

/* enable misaligned memory access */
static bool opt_misaligned = false;

/* dump profiling data */
static bool opt_prof_data = false;
static char *prof_out_file;

#if RV32_HAS(SYSTEM_MMIO)
/* Linux kernel data */
static char *opt_kernel_img;
static char *opt_rootfs_img;
static char *opt_bootargs;
/* FIXME: handle overflow */
#define VBLK_DEV_MAX 100
static char *opt_virtio_blk_img[VBLK_DEV_MAX];
static int opt_virtio_blk_idx = 0;
#endif

static void print_usage(const char *filename)
{
    rv_log_error(
        "\nRV32I[MAFC] Emulator which loads an ELF file to execute.\n"
        "Usage: %s [options] [filename] [arguments]\n"
        "Options:\n"
#if !RV32_HAS(SYSTEM_MMIO)
        "  -t : print executable trace\n"
#endif
#if RV32_HAS(GDBSTUB)
        "  -g : allow remote GDB connections (as gdbstub)\n"
#endif
#if RV32_HAS(SYSTEM_MMIO)
        "  -k <image> : use <image> as kernel image\n"
        "  -i <image> : use <image> as rootfs\n"
        "  -x vblk:<image>[,readonly]: use "
        "<image> as virtio-blk disk image "
        "(default read and write). This option may be specified "
        "multiple times for multiple block devices\n"
        "  -b <bootargs> : use customized <bootargs> for the kernel\n"
#endif
        "  -d [filename]: dump registers as JSON to the "
        "given file or `-` (STDOUT)\n"
        "  -q : Suppress outputs other than `dump-registers`\n"
        "  -a [filename] : dump signature to the given file, "
        "required by arch-test test\n"
        "  -m : enable misaligned memory access\n"
        "  -p : generate profiling data\n"
        "  -h : show this message",
        filename);
}

static bool parse_args(int argc, char **args)
{
    int opt;
    int emu_argc = 0;

    while ((opt = getopt(argc, args, optstr)) != -1) {
        emu_argc++;

        switch (opt) {
#if !RV32_HAS(SYSTEM_MMIO)
        case 't':
            opt_trace = true;
            break;
#endif
#if RV32_HAS(GDBSTUB)
        case 'g':
            opt_gdbstub = true;
            break;
#endif
#if RV32_HAS(SYSTEM_MMIO)
        case 'k':
            opt_kernel_img = optarg;
            emu_argc++;
            break;
        case 'i':
            opt_rootfs_img = optarg;
            emu_argc++;
            break;
        case 'b':
            opt_bootargs = optarg;
            emu_argc++;
            break;
        case 'x':
            if (opt_virtio_blk_idx >= VBLK_DEV_MAX) {
                rv_log_error("Too many virtio-blk devices. Maximum is %d.\n",
                             VBLK_DEV_MAX);
                return false;
            }
            if (!strncmp("vblk:", optarg, 5))
                opt_virtio_blk_img[opt_virtio_blk_idx++] =
                    optarg + 5; /* strlen("vblk:") */
            else
                return false;
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
        size_t args0_len = strlen(args[0]);
        /* Ensure args[0] is long enough before subtracting */
        if (args0_len > 7) { /* strlen("rv32emu") */
            size_t copy_len = args0_len - 7;
            if (copy_len >= PATH_MAX)
                copy_len = PATH_MAX - 1;
            memcpy(rel_path, args[0], copy_len);
            rel_path[copy_len] = '\0';
        }

        char *prog_basename = basename(opt_prog_name);
        size_t total_len = strlen(cwd_path) + 1 + strlen(rel_path) +
                           strlen(prog_basename) + 5 + 1;
        prof_out_file = malloc(total_len);
        if (!prof_out_file) {
            rv_log_error("Failed to allocate profiling output filename");
            return false;
        }
        assert(prof_out_file);

        snprintf(prof_out_file, total_len, "%s/%s%s.prof", cwd_path, rel_path,
                 prog_basename);
    }
    return true;
}

static void dump_test_signature(const char UNUSED *prog_name)
{
    elf_t *elf = elf_new();
    assert(elf && elf_open(elf, prog_name));

    uint32_t start = 0, end = 0;
    const struct Elf32_Sym *sym;
    FILE *f = fopen(signature_out_file, "w");
    if (!f) {
        rv_log_fatal("Cannot open signature output file: %s",
                     signature_out_file);
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
/* MEM_SIZE is defined by Makefile:
 * - SYSTEM mode (kernel): configurable, default 512 MiB
 * - User-mode: configurable via USER_MEM_SIZE, default 256 MiB
 * With demand paging, physical memory is allocated only when accessed.
 */
#ifndef MEM_SIZE
#define MEM_SIZE (256ULL * 1024 * 1024) /* 256 MiB default */
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

int main(int argc, char **args)
{
    if (argc == 1 || !parse_args(argc, args)) {
        print_usage(args[0]);
        return 1;
    }

    int run_flag = 0;
#if !RV32_HAS(SYSTEM_MMIO)
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
        .log_level = LOG_WARN,
        .run_flag = run_flag,
        .profile_output_file = prof_out_file,
        .cycle_per_step = CYCLE_PER_STEP,
        .allow_misalign = opt_misaligned,
        .fd_stdin = STDIN_FILENO,
        .fd_stdout = STDOUT_FILENO,
        .fd_stderr = STDERR_FILENO,
    };
#if RV32_HAS(SYSTEM_MMIO)
    attr.data.system.kernel = opt_kernel_img;
    attr.data.system.initrd = opt_rootfs_img;
    attr.data.system.bootargs = opt_bootargs;
    if (opt_virtio_blk_idx) {
        attr.data.system.vblk_device = opt_virtio_blk_img;
        attr.data.system.vblk_device_cnt = opt_virtio_blk_idx;
    } else {
        attr.data.system.vblk_device = NULL;
    }
#else
    attr.data.user.elf_program = opt_prog_name;
#endif

    /* enable or disable the logging outputs */
    rv_log_set_quiet(opt_quiet_outputs);

    /* create the RISC-V runtime */
    rv = rv_create(&attr);
    if (!rv) {
        rv_log_fatal("Unable to create riscv emulator");
        attr.exit_code = 1;
        goto end;
    }
    rv_log_info("RISC-V emulator is created and ready to run");

#if RV32_HAS(ARCH_TEST)
    /* Extract tohost/fromhost addresses for arch-test mode */
    if (opt_arch_test && opt_prog_name) {
        elf_t *elf = elf_new();
        if (elf && elf_open(elf, opt_prog_name)) {
            const struct Elf32_Sym *sym;
            if ((sym = elf_get_symbol(elf, "tohost"))) {
                rv_set_tohost_addr(rv, sym->st_value);
                rv_log_info("Found tohost at 0x%08x", sym->st_value);
            }
            if ((sym = elf_get_symbol(elf, "fromhost"))) {
                rv_set_fromhost_addr(rv, sym->st_value);
                rv_log_info("Found fromhost at 0x%08x", sym->st_value);
            }
        }
        elf_delete(elf);
    }
#endif

#if defined(__EMSCRIPTEN__)
    disable_run_button();
#endif

    rv_run(rv);

    /* dump registers as JSON */
    if (opt_dump_regs)
        dump_registers(rv, registers_out_file);

    /* dump test result in test mode */
    if (opt_arch_test)
        dump_test_signature(opt_prog_name);

    /* finalize the RISC-V runtime */
    rv_delete(rv);
    /*
     * Other translation units cannot update the pointer, update it here
     * to prevent multiple atexit()'s callback be called.
     */
    rv = NULL;
    uint64_t mem_usage = memory_get_usage();
    rv_log_info("Peak memory usage: %" PRIu64 " KB (%" PRIu64 " MB)",
                mem_usage / 1024, mem_usage / (1024 * 1024));
    rv_log_info("RISC-V emulator is destroyed");

end:
    free(prof_out_file);
    return attr.exit_code;
}
