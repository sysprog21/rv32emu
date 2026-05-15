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
EM_JS(void, report_run_completion, (), {});
#else
EM_JS(void, enable_run_button, (), {
    document.getElementById('runButton').disabled = false;
    document.getElementById('stopButton').disabled = true;
});
EM_JS(void, disable_run_button, (), {
    document.getElementById('runButton').disabled = true;
});
EM_JS(void, report_run_completion, (), {
    var statusText = document.getElementById('statusText');
    var statusBadge = document.getElementById('statusBadge');
    if (statusText)
        statusText.textContent = 'Completed';
    if (statusBadge)
        statusBadge.classList.remove('running');
    /* clang-format mangles JS strict-inequality (!==) here because it parses
     * the EM_JS body as C. Use the loose form; typeof always returns a
     * string so the two are equivalent for the 'undefined' check. */
    if (typeof term != 'undefined' && term) {
        term.write('\x1b[32m> Completed\x1b[0m\r\n');
    }
});
#endif

#if RV32_HAS(SYSTEM_MMIO)
extern uint8_t input_buf_size;

char *get_input_buf();
uint8_t get_input_buf_cap();
void set_input_buf_size(uint8_t size);
uint8_t get_input_buf_size();
void u8250_put_rx_char(uint8_t c);
#endif
#endif
