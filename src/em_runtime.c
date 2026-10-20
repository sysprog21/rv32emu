/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include "em_runtime.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/eventloop.h>
#include <emscripten/html5.h>

#if RV32_HAS(SYSTEM)
EM_JS(void, enable_run_button, (void), {
    document.getElementById('runSysButton').disabled = false;
});
EM_JS(void, disable_run_button, (void), {
    document.getElementById('runSysButton').disabled = true;
});
EM_JS(void, report_run_completion, (void), {});
#else
EM_JS(void, enable_run_button, (void), {
    document.getElementById('runButton').disabled = false;
    document.getElementById('stopButton').disabled = true;
});
EM_JS(void, disable_run_button, (void), {
    document.getElementById('runButton').disabled = true;
});
EM_JS(void, report_run_completion, (void), {
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

/* Node and workers have no document to watch. */
static bool has_document(void)
{
    return EM_ASM_INT({ return !!globalThis.document; });
}

/* Message tasks keep running in a hidden tab. Switch to animation frames, which
 * the browser pauses there, until the page is visible again.
 */
static EM_BOOL visibility_changed(int type,
                                  const EmscriptenVisibilityChangeEvent *ev,
                                  void *user)
{
    (void) type;
    (void) user;
    if (ev->hidden)
        emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
    else
        emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 0);
    return EM_FALSE;
}

void em_run_main_loop(em_arg_callback_func step, void *arg)
{
    /* step limits host time per slice. Message tasks yield to input and
     * rendering without the browser's nested-timer minimum delay.
     */
    emscripten_set_main_loop_arg(step, arg, 1000, 0);
    emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 0);
    if (has_document()) {
        EmscriptenVisibilityChangeEvent ev;
        if (emscripten_get_visibility_status(&ev) == EMSCRIPTEN_RESULT_SUCCESS)
            visibility_changed(EMSCRIPTEN_EVENT_VISIBILITYCHANGE, &ev, NULL);
        emscripten_set_visibilitychange_callback(NULL, EM_FALSE,
                                                 visibility_changed);
    }
    emscripten_unwind_to_js_event_loop();
}

void em_stop_main_loop(void)
{
    emscripten_cancel_main_loop();
    /* Without a main loop, a timing change would only log an error. */
    if (has_document())
        emscripten_set_visibilitychange_callback(NULL, EM_FALSE, NULL);
}
#endif
