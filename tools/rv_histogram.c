/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "decode.h"
#include "elf.h"

static bool ascending_order = false;
static const char *elf_prog = NULL;

typedef struct {
    char insn_str[16];
    size_t freq;
} rv_insn_hist_t;

static rv_insn_hist_t rv_insn_stats[] = {
#define _(inst, can_branch) {#inst, 0},
    RISCV_INSN_LIST _(unknown, 0)
#undef _
};

static int cmp_dec(const void *a, const void *b)
{
    return ((rv_insn_hist_t *) b)->freq - ((rv_insn_hist_t *) a)->freq;
}

static int cmp_asc(const void *a, const void *b)
{
    return ((rv_insn_hist_t *) a)->freq - ((rv_insn_hist_t *) b)->freq;
}

/* used to adjust the length of histogram bar */
static unsigned short get_win_max_col()
{
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize terminal_size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal_size);
    return terminal_size.ws_col;
#endif
}

static const char *fmt = "%3d. %-10s%5.2f%% [%-10zu] %s\n";

/* get columns used by the fmt string */
static unsigned short get_used_col()
{
    unsigned short used_col = 0;

    used_col += 3;  /* width = 3 */
    used_col += 1;  /* . */
    used_col += 1;  /* single space */
    used_col += 10; /* width = 10 */
    used_col += 5;  /* width = 5 */
    used_col += 1;  /* % */
    used_col += 1;  /* single space */
    used_col += 1;  /* [ */
    used_col += 10; /* width = 10 */
    used_col += 1;  /* ] */
    used_col += 1;  /* single space */

    return used_col;
}

static char *gen_hist_bar(char *hist_bar,
                          size_t hist_bar_len,
                          size_t insn_freq,
                          size_t max_insn_freq,
                          unsigned short max_col,
                          unsigned short used_col)
{
#if defined(_WIN32)
    size_t v = insn_freq * (max_col - used_col) / max_insn_freq;
    for (size_t i = 0; i < v; i++) {
        hist_bar[i] = '*';
    }
    hist_bar[v] = 0;
#else
    const char *a[] = {" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};
    size_t v = insn_freq * (max_col - used_col) * 8 / max_insn_freq;
    hist_bar[0] = '\0';
    while (v > 8) {
        strncat(hist_bar, a[8], hist_bar_len--);
        v -= 8;
    }
    strncat(hist_bar, a[v], hist_bar_len--);
#endif

    return hist_bar;
}

static void print_usage(const char *filename)
{
    fprintf(stderr,
            "rv_histogram which loads an ELF file to execute.\n"
            "Usage: %s [option] [elf_file_path]\n"
            "available option: -a, print the histogram in ascending order\n",
            filename);
}

static bool parse_args(int argc, char **args)
{
    const char *optstr = "a";
    int opt;

    while ((opt = getopt(argc, args, optstr)) != -1) {
        switch (opt) {
        case 'a':
            ascending_order = true;
            break;
        default:
            return false;
        }
    }

    elf_prog = args[optind];

    return true;
}

int main(int argc, char *args[])
{
    if (!parse_args(argc, args) || !elf_prog) {
        print_usage(args[0]);
        return 1;
    }

    elf_t *e = elf_new();
    if (!elf_open(e, elf_prog)) {
        fprintf(stderr, "elf_open failed\n");
        return 1;
    }

    struct Elf32_Ehdr *hdr = get_elf_header(e);
    struct Elf32_Shdr **shdrs = get_elf_section_headers(e);
    if (!shdrs) {
        fprintf(stderr, "malloc for section headers failed\n");
        return 1;
    }
    uintptr_t *elf_first_byte = (uintptr_t *) get_elf_first_byte(e);
    size_t total_insn = 0;
    rv_insn_t ir;
    bool res;

    for (int i = 0; i < hdr->e_shnum; i++) {
        struct Elf32_Shdr *shdr = (struct Elf32_Shdr *) shdrs + i;

        if (!(shdr->sh_type & SHT_PROGBITS && shdr->sh_flags & SHF_EXECINSTR))
            continue;

        uintptr_t *exec_start_addr =
            (uintptr_t *) ((uint8_t *) elf_first_byte + shdr->sh_offset);
        uintptr_t *exec_end_addr =
            (uintptr_t *) ((uint8_t *) exec_start_addr + shdr->sh_size);
        uintptr_t *ptr = (uintptr_t *) exec_start_addr;
        uint32_t insn;

        while (ptr < exec_end_addr) {
#if RV32_HAS(EXT_C)
            if ((*((uint32_t *) ptr) & FC_OPCODE) != 0x3) {
                insn = *((uint16_t *) ptr);
                ptr = (uintptr_t *) ((uint8_t *) ptr + 2);
                goto decode;
            }
#endif
            insn = *((uint32_t *) ptr);
            ptr = (uintptr_t *) ((uint8_t *) ptr + 4);

#if RV32_HAS(EXT_C)
        decode:
#endif
            res = rv_decode(&ir, insn);

            /* unknown instruction */
            if (!res) {
                rv_insn_stats[N_RISCV_INSN_LIST].freq++;
                continue;
            }

            rv_insn_stats[ir.opcode].freq++;
            total_insn++;
        }
    }

    /* decending order on default */
    qsort(rv_insn_stats, ARRAYS_SIZE(rv_insn_stats), sizeof(rv_insn_hist_t),
          ascending_order ? cmp_asc : cmp_dec);

    unsigned short max_col = get_win_max_col();
    unsigned short used_col = get_used_col();
    char hist_bar[max_col * 3 + 1];
    char *insn_str;
    size_t insn_freq;
    size_t max_insn_freq = 0;
    float percent;

    /* 1 for unknown */
    for (int i = 0; i < N_RISCV_INSN_LIST + 1; i++) {
        if (rv_insn_stats[i].freq > max_insn_freq)
            max_insn_freq = rv_insn_stats[i].freq;
    }

    printf("+---------------------------------------------+\n");
    printf("| RV32 Target Instruction Frequency Histogram |\n");
    printf("+---------------------------------------------+\n");

    /* 1 for unknown */
    for (int i = 0; i < N_RISCV_INSN_LIST + 1; i++) {
        insn_str = rv_insn_stats[i].insn_str;
        insn_freq = rv_insn_stats[i].freq;

        percent = (float) insn_freq / total_insn;
        if (percent < 0.01)
            continue;

        printf(fmt, i + 1, insn_str, percent, insn_freq,
               gen_hist_bar(hist_bar, sizeof(hist_bar), insn_freq,
                            max_insn_freq, max_col, used_col));
    }

    free(shdrs);
    elf_delete(e);

    return 0;
}
