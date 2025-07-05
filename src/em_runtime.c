/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include "em_runtime.h"

#if defined(__EMSCRIPTEN__)
#if RV32_HAS(SYSTEM)
EM_JS(void, enable_run_button, (), {
    document.getElementById('runSysButton').disabled = false;
});
EM_JS(void, disable_run_button, (), {
    document.getElementById('runSysButton').disabled = true;
});
#else
EM_JS(void, enable_run_button, (), {
    document.getElementById('runButton').disabled = false;
});
EM_JS(void, disable_run_button, (), {
    document.getElementById('runButton').disabled = true;
});
#endif

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
extern uint8_t input_buf_size;

char *get_input_buf();
uint8_t get_input_buf_cap();
void set_input_buf_size(uint8_t size);
#endif
#endif
