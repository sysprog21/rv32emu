#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "context.h"

void helper_context(void);

void initialize_context(context_t ctx,
                        void *stack_base,
                        size_t stack_size,
                        void (*entry)(void *data),
                        void *data)
{
    /* FIXME: only valid for AMD64 ABI (and RISC-V?)
     * https://software.intel.com/en-us/forums/intel-isa-extensions/topic/291241
     */
#define RED_ZONE 128
    uintptr_t stack_end = (uintptr_t) stack_base + stack_size - RED_ZONE;
    stack_end &= -16; /* ensure that the stack is 16-byte aligned */

    memset(ctx, 0, sizeof *ctx);
    ctx->sp = stack_end;
    ctx->ra = (uintptr_t) helper_context;
    ctx->data = data;
    ctx->entry = entry;
}
