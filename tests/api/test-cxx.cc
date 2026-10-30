/* SPDX-License-Identifier: MIT */
/* Include first and twice: the public header must be self-contained and
 * guarded. Calling these functions also checks C linkage against the actual
 * library.
 */
#include <rv32emu.h>
#include <cassert>
#include <cstring>

int main()
{
    rv32emu_options_t options;
    rv32emu_options_init(&options);
    assert(options.memory_size == (64U << 20));
    assert(options.argc == 0 && options.argv == nullptr);
    rv32emu_t *emu = nullptr;
    assert(rv32emu_create(nullptr, &options, &emu) == RV32EMU_INVALID_ARGUMENT);
    assert(emu == nullptr);
    assert(std::strcmp(rv32emu_status_string(RV32EMU_OK), "success") == 0);
    rv32emu_destroy(emu);
}
