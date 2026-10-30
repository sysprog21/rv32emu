/* SPDX-License-Identifier: MIT */
#include <rv32emu.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s program.elf [args...]\n", argv[0]);
        return 1;
    }
    rv32emu_options_t options;
    rv32emu_options_init(&options);
    options.argc = argc - 1;
    options.argv = (const char *const *) (argv + 1);
    rv32emu_t *emu;
    rv32emu_status_t status = rv32emu_create(argv[1], &options, &emu);
    if (status != RV32EMU_OK) {
        fprintf(stderr, "%s\n", rv32emu_status_string(status));
        return 1;
    }
    while ((status = rv32emu_step(emu, 1000)) == RV32EMU_OK)
        ; /* The host may do other work between slices. */
    int code = 1;
    if (status == RV32EMU_HALTED)
        rv32emu_exit_code(emu, &code);
    rv32emu_destroy(emu);
    return code;
}
