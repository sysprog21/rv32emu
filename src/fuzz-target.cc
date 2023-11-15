#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "riscv.h"

const int max_cycles = 5000;
const char *fake_rv32emu_name = "./fake_rv32emu";
const char *fake_elf_name = "fake_elf";

/* In order to be able to inspect a coredump we want to crash on every ASAN
 * error.
 */
extern "C" void __asan_on_error()
{
    abort();
}
extern "C" void __msan_on_error()
{
    abort();
}

static void fuzz_elf_loader(const uint8_t *data, size_t len)
{
    int argc = 1 + 2 * 3 + 1;
    char **args = (char **) malloc(sizeof(char *) * argc);

    char *arg0 = (char *) malloc(strlen(fake_rv32emu_name) + 1);
    strncpy(arg0, fake_rv32emu_name, strlen(fake_rv32emu_name) + 1);
    args[0] = arg0;

    char *arg1 = (char *) malloc(3);
    strncpy(arg1, "-s", 3);
    args[1] = arg1;
    args[2] = (char *) data;

    char *arg3 = (char *) malloc(3);
    strncpy(arg3, "-l", 3);
    args[3] = arg3;
    char *len_str =
        (char *) malloc(20 + 1); /* LLONG_MIN in base 10 has 20 chars */
    sprintf(len_str, "%zu", len);
    args[4] = len_str;

    char *arg5 = (char *) malloc(3);
    strncpy(arg5, "-k", 3);
    args[5] = arg5;
    char *max_cycles_str =
        (char *) malloc(11 + 1); /* INT_MIN in base 10 has 11 chars */
    sprintf(max_cycles_str, "%d", max_cycles);
    args[6] = max_cycles_str;

    char *arg7 = (char *) malloc(strlen(fake_elf_name) + 1);
    strncpy(arg7, fake_elf_name, strlen(fake_elf_name) + 1);
    args[7] = arg7;

    int ret = rv_init_and_execute_elf(argc, args);
    if (ret == 0) {
        fprintf(stderr, "Executed successfully\n");
    } else {
        fprintf(stderr, "Executed with failure\n");
    }

    free(arg0);
    free(arg1);
    free(arg3);
    free(len_str);
    free(arg5);
    free(max_cycles_str);
    free(arg7);
    free(args);
}

extern "C" void LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
    fuzz_elf_loader(data, len);
}
