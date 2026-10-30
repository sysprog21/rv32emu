/* SPDX-License-Identifier: MIT */
#include <rv32emu.h>
#include <stdio.h>

/* Run existing guest regressions through the same interface as an embedder.
 * A slice cap turns a nonterminating guest into a test failure.
 */
int main(int argc, char **argv)
{
    if (argc != 2)
        return 1;
    rv32emu_t *emu = NULL;
    rv32emu_status_t status = rv32emu_create(argv[1], NULL, &emu);
    if (status != RV32EMU_OK) {
        fprintf(stderr, "%s: %s\n", argv[1], rv32emu_status_string(status));
        return 1;
    }
    for (unsigned slice = 0; slice < 100000 && status == RV32EMU_OK; slice++)
        status = rv32emu_step(emu, 10000);
    int code = 1;
    if (status != RV32EMU_HALTED ||
        rv32emu_exit_code(emu, &code) != RV32EMU_OK) {
        fprintf(stderr, "%s: guest did not halt successfully (%s)\n", argv[1],
                rv32emu_status_string(status));
        code = 1;
    }
    rv32emu_destroy(emu);
    return code;
}
