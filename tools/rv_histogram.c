/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

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

typedef void (*hist_record_handler)(const rv_insn_t *);

static bool ascending_order = false;
static bool show_reg = false;
static const char *elf_prog = NULL;

static size_t max_freq = 0;
static size_t total_freq = 0;
static unsigned short max_col;
static unsigned short used_col;

typedef struct {
    char insn_reg[16]; /* insn or reg   */
    size_t freq;       /* frequency     */
    uint8_t reg_mask;  /* 0x1=rs1, 0x2=rs2, 0x4=rs3, 0x8=rd */
} rv_hist_t;

/* clang-format off */
static rv_hist_t rv_insn_stats[] = {
#define _(inst, can_branch, insn_len, translatable, reg_mask) {#inst, 0, reg_mask},
    RV_INSN_LIST
    _(unknown, 0, 0, 0, 0)
#undef _
};
/* clang-format on */

static rv_hist_t rv_reg_stats[] = {
#define _(reg) {#reg, 0},
    RV_REGS_LIST
#undef _
};

static int cmp_dec(const void *a, const void *b)
{
    return ((rv_hist_t *) b)->freq - ((rv_hist_t *) a)->freq;
}

static int cmp_asc(const void *a, const void *b)
{
    return ((rv_hist_t *) a)->freq - ((rv_hist_t *) b)->freq;
}

/* used to adjust the length of histogram bar */
static unsigned short get_win_max_col(void)
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

static void find_max_freq(const rv_hist_t *stats, size_t stats_size)
{
    for (size_t i = 0; i < stats_size; i++) {
        if (stats[i].freq > max_freq)
            max_freq = stats[i].freq;
    }
}

static const char *fmt = "%3d. %-10s%5.2f%% [%-10zu] %s\n";

/* get columns used by the fmt string */
static unsigned short get_used_col(void)
{
    unsigned short used_col = 0;

    used_col += 3u;  /* width = 3 */
    used_col += 1u;  /* . */
    used_col += 1u;  /* single space */
    used_col += 10u; /* width = 10 */
    used_col += 5u;  /* width = 5 */
    used_col += 1u;  /* % */
    used_col += 1u;  /* single space */
    used_col += 1u;  /* [ */
    used_col += 10u; /* width = 10 */
    used_col += 1u;  /* ] */
    used_col += 1u;  /* single space */

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
            "available options: -a, print the histogram in "
            "ascending order(default is on descending order)\n"
            "                 : -r, analysis on registers\n",
            filename);
}

/* FIXME: refactor to elegant code */
static bool parse_args(int argc, const char *args[])
{
    bool ret = true;
    for (int i = 1; (i < argc) && ret; i++) {
        const char *arg = args[i];
        if (arg[0] == '-') {
            if (strstr(arg, "-ar") || strstr(arg, "-ra")) {
                ascending_order = true;
                show_reg = true;
                continue;
            }

            if (!strcmp(arg, "-a")) {
                ascending_order = true;
                continue;
            }

            if (!strcmp(arg, "-r")) {
                show_reg = true;
                continue;
            }

            ret = false;
        }
        /* set the executable */
        elf_prog = arg;
    }

    return ret;
}

static void print_hist_stats(const rv_hist_t *stats, size_t stats_size)
{
    char hist_bar[max_col * 3 + 1];
    float percent;
    size_t idx = 1;

    for (size_t i = 0; i < stats_size; i++) {
        const char *insn_reg = stats[i].insn_reg;
        size_t freq = stats[i].freq;

        percent = ((float) freq / total_freq) * 100;
        if (percent < 1.00)
            continue;

        printf(fmt, idx, insn_reg, percent, freq,
               gen_hist_bar(hist_bar, sizeof(hist_bar), freq, max_freq, max_col,
                            used_col));
        idx++;
    }
}

static void reg_hist_incr(const rv_insn_t *ir)
{
    if (!ir)
        return;

    uint8_t reg_mask = rv_insn_stats[ir->opcode].reg_mask;

    if (reg_mask & F_rs1) {
        rv_reg_stats[ir->rs1].freq++;
        total_freq++;
    }
    if (reg_mask & F_rs2) {
        rv_reg_stats[ir->rs2].freq++;
        total_freq++;
    }
    if (reg_mask & F_rs3) {
        rv_reg_stats[ir->rs3].freq++;
        total_freq++;
    }
    if (reg_mask & F_rd) {
        rv_reg_stats[ir->rd].freq++;
        total_freq++;
    }
}

static void insn_hist_incr(const rv_insn_t *ir)
{
    if (!ir) {
        rv_insn_stats[N_RV_INSNS].freq++;
        return;
    }
    rv_insn_stats[ir->opcode].freq++;
    total_freq++;
}

int main(int argc, const char *args[])
{
    if (!parse_args(argc, args) || !elf_prog) {
        print_usage(args[0]);
        return 1;
    }

    /* resolver of histogram accounting */
    hist_record_handler hist_record = show_reg ? reg_hist_incr : insn_hist_incr;

    elf_t *e = elf_new();
    if (!elf_open(e, elf_prog)) {
        (void) fprintf(stderr, "Failed to open %s\n", elf_prog);
        return 1;
    }

    struct Elf32_Ehdr *hdr = get_elf_header(e);
    if (!hdr->e_shnum) {
        (void) fprintf(stderr, "no section headers are found in %s\n",
                       elf_prog);
        return 1;
    }

    uint8_t *elf_first_byte = get_elf_first_byte(e);
    const struct Elf32_Shdr **shdrs =
        (const struct Elf32_Shdr **) &elf_first_byte[hdr->e_shoff];

    rv_insn_t ir;
    bool res;

    for (int i = 0; i < hdr->e_shnum; i++) {
        const struct Elf32_Shdr *shdr = (const struct Elf32_Shdr *) &shdrs[i];
        bool is_prg = shdr->sh_type & SHT_PROGBITS;
        bool has_insn = shdr->sh_flags & SHF_EXECINSTR;

        if (!(is_prg && has_insn))
            continue;

        uint8_t *exec_start_addr = &elf_first_byte[shdr->sh_offset];
        const uint8_t *exec_end_addr = &exec_start_addr[shdr->sh_size];
        uint8_t *ptr = exec_start_addr;
        uint32_t insn;

        while (ptr < exec_end_addr) {
#if RV32_HAS(EXT_C)
            if ((*((uint32_t *) ptr) & FC_OPCODE) != 0x3) {
                insn = *((uint16_t *) ptr);
                ptr += 2;
                goto decode;
            }
#endif
            insn = *((uint32_t *) ptr);
            ptr += 4;


#if RV32_HAS(EXT_C)
        decode:
#endif
            res = rv_decode(&ir, insn);

            /* unknown instruction */
            if (!res) {
                hist_record(NULL);
                continue;
            }
            hist_record(&ir);
        }
    }

    max_col = get_win_max_col();
    used_col = get_used_col();

    if (show_reg) {
        qsort(rv_reg_stats, ARRAY_SIZE(rv_reg_stats), sizeof(rv_hist_t),
              ascending_order ? cmp_asc : cmp_dec);

        printf("+--------------------------------------+\n");
        printf("| RV32 Target Register Usage Histogram |\n");
        printf("+--------------------------------------+\n");
        find_max_freq(rv_reg_stats, N_RV_REGS);
        print_hist_stats(rv_reg_stats, N_RV_REGS);
    } else {
        qsort(rv_insn_stats, ARRAY_SIZE(rv_insn_stats), sizeof(rv_hist_t),
              ascending_order ? cmp_asc : cmp_dec);

        printf("+---------------------------------------------+\n");
        printf("| RV32 Target Instruction Frequency Histogram |\n");
        printf("+---------------------------------------------+\n");
        find_max_freq(rv_insn_stats, N_RV_INSNS + 1);
        print_hist_stats(rv_insn_stats, N_RV_INSNS + 1);
    }

    elf_delete(e);

    return 0;
}
