/* Lightweight coroutine for multi-hart execution */

#include "coro.h"

/* Builds without hart coroutines still compile this file, so that the build
 * does not have to repeat the platform condition from feature.h.
 */
#if RV32_HAS_HART_CORO

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Platform detection */

#if !defined(CORO_USE_UCONTEXT) && !defined(CORO_USE_ASM)
#if defined(__x86_64__) || defined(__aarch64__)
#define CORO_USE_ASM
#else
#define CORO_USE_UCONTEXT
#endif
#endif

/* Coroutine state */

typedef enum {
    CORO_STATE_SUSPENDED,
    CORO_STATE_RUNNING,
    CORO_STATE_DEAD
} coro_state_t;

/* Platform-specific context buffer and assembly implementation */

#ifdef CORO_USE_ASM

#if defined(__x86_64__)
/* x86-64 context buffer - stores callee-saved registers */
typedef struct {
    void *rip, *rsp, *rbp, *rbx, *r12, *r13, *r14, *r15;
} coro_ctxbuf_t;

/* Forward declarations for assembly functions */
void _coro_wrap_main(void);
int _coro_switch(coro_ctxbuf_t *from, coro_ctxbuf_t *to);

/* Assembly implementation for x86-64 (macOS and Linux) */
__asm__(
    ".text\n"
#ifdef __MACH__ /* macOS assembler */
    ".globl __coro_wrap_main\n"
    "__coro_wrap_main:\n"
#else /* Linux assembler */
    ".globl _coro_wrap_main\n"
    ".type _coro_wrap_main @function\n"
    ".hidden _coro_wrap_main\n"
    "_coro_wrap_main:\n"
#endif
    "  movq %r13, %rdi\n" /* Load coroutine pointer into first argument */
    "  jmpq *%r12\n"      /* Jump to the coroutine entry point */
#ifndef __MACH__
    ".size _coro_wrap_main, .-_coro_wrap_main\n"
#endif
);

__asm__(
    ".text\n"
#ifdef __MACH__ /* macOS assembler */
    ".globl __coro_switch\n"
    "__coro_switch:\n"
#else /* Linux assembler */
    ".globl _coro_switch\n"
    ".type _coro_switch @function\n"
    ".hidden _coro_switch\n"
    "_coro_switch:\n"
#endif
    /* Save current context (first argument: from) */
    "  leaq 1f(%rip), %rax\n" /* Load return address */
    "  movq %rax, (%rdi)\n"   /* Save RIP */
    "  movq %rsp, 8(%rdi)\n"  /* Save RSP */
    "  movq %rbp, 16(%rdi)\n" /* Save RBP */
    "  movq %rbx, 24(%rdi)\n" /* Save RBX */
    "  movq %r12, 32(%rdi)\n" /* Save R12 */
    "  movq %r13, 40(%rdi)\n" /* Save R13 */
    "  movq %r14, 48(%rdi)\n" /* Save R14 */
    "  movq %r15, 56(%rdi)\n" /* Save R15 */
    /* Restore new context (second argument: to) */
    "  movq 56(%rsi), %r15\n" /* Restore R15 */
    "  movq 48(%rsi), %r14\n" /* Restore R14 */
    "  movq 40(%rsi), %r13\n" /* Restore R13 */
    "  movq 32(%rsi), %r12\n" /* Restore R12 */
    "  movq 24(%rsi), %rbx\n" /* Restore RBX */
    "  movq 16(%rsi), %rbp\n" /* Restore RBP */
    "  movq 8(%rsi), %rsp\n"  /* Restore RSP */
    "  jmpq *(%rsi)\n"        /* Jump to saved RIP */
    "1:\n"
    "  ret\n"
#ifndef __MACH__
    ".size _coro_switch, .-_coro_switch\n"
#endif
);

#elif defined(__aarch64__)

/* ARM64 context buffer - stores callee-saved registers */
typedef struct {
    void *x[12]; /* x19-x30 */
    void *sp;
    void *lr;
    void *d[8]; /* d8-d15 (floating point) */
} coro_ctxbuf_t;

/* Forward declarations for assembly functions */
void _coro_wrap_main(void);
int _coro_switch(coro_ctxbuf_t *from, coro_ctxbuf_t *to);

/* Assembly implementation for ARM64 (macOS and Linux) */
__asm__(
    ".text\n"
#ifdef __APPLE__
    ".globl __coro_switch\n"
    "__coro_switch:\n"
#else
    ".globl _coro_switch\n"
    ".type _coro_switch #function\n"
    ".hidden _coro_switch\n"
    "_coro_switch:\n"
#endif
    /* Save current context (x0 = from) */
    "  mov x10, sp\n"
    "  mov x11, x30\n"
    "  stp x19, x20, [x0, #(0*16)]\n"
    "  stp x21, x22, [x0, #(1*16)]\n"
    "  stp d8, d9, [x0, #(7*16)]\n"
    "  stp x23, x24, [x0, #(2*16)]\n"
    "  stp d10, d11, [x0, #(8*16)]\n"
    "  stp x25, x26, [x0, #(3*16)]\n"
    "  stp d12, d13, [x0, #(9*16)]\n"
    "  stp x27, x28, [x0, #(4*16)]\n"
    "  stp d14, d15, [x0, #(10*16)]\n"
    "  stp x29, x30, [x0, #(5*16)]\n"
    "  stp x10, x11, [x0, #(6*16)]\n"
    /* Restore new context (x1 = to) */
    "  ldp x19, x20, [x1, #(0*16)]\n"
    "  ldp x21, x22, [x1, #(1*16)]\n"
    "  ldp d8, d9, [x1, #(7*16)]\n"
    "  ldp x23, x24, [x1, #(2*16)]\n"
    "  ldp d10, d11, [x1, #(8*16)]\n"
    "  ldp x25, x26, [x1, #(3*16)]\n"
    "  ldp d12, d13, [x1, #(9*16)]\n"
    "  ldp x27, x28, [x1, #(4*16)]\n"
    "  ldp d14, d15, [x1, #(10*16)]\n"
    "  ldp x29, x30, [x1, #(5*16)]\n"
    "  ldp x10, x11, [x1, #(6*16)]\n"
    "  mov sp, x10\n"
    "  br x11\n"
#ifndef __APPLE__
    ".size _coro_switch, .-_coro_switch\n"
#endif
);

__asm__(
    ".text\n"
#ifdef __APPLE__
    ".globl __coro_wrap_main\n"
    "__coro_wrap_main:\n"
#else
    ".globl _coro_wrap_main\n"
    ".type _coro_wrap_main #function\n"
    ".hidden _coro_wrap_main\n"
    "_coro_wrap_main:\n"
#endif
    "  mov x0, x19\n"  /* Load coroutine pointer into first argument */
    "  mov x30, x21\n" /* Set return address */
    "  br x20\n"       /* Branch to the coroutine entry point */
#ifndef __APPLE__
    ".size _coro_wrap_main, .-_coro_wrap_main\n"
#endif
);

#else
#error "Unsupported architecture for assembly method"
#endif

#elif defined(CORO_USE_UCONTEXT)

/* ucontext fallback for other platforms */
#include <ucontext.h>

typedef ucontext_t coro_ctxbuf_t;

#else
#error "No coroutine implementation available for this platform"
#endif

/* Internal context structure */

typedef struct {
    coro_ctxbuf_t ctx;      /* Coroutine context */
    coro_ctxbuf_t back_ctx; /* Caller context (to return to) */
} coro_context_t;

/* Internal coroutine structure */

typedef struct {
    void (*func)(void *);   /* Entry point function (user-provided) */
    void *user_data;        /* User data (hart pointer) */
    coro_state_t state;     /* Current state */
    coro_context_t context; /* Context buffers */
    void *stack_base;       /* Stack base address (above the guard region) */
    size_t stack_size;      /* Stack size */
    void *map_base;         /* Mapping base, including the guard region */
    size_t map_size;        /* Mapping size, including the guard region */
    bool restart;
} coro_t;

/* Global state */

static struct {
    coro_t **coroutines;  /* Array of coroutine pointers */
    uint32_t total_slots; /* Total number of coroutine slots */
    bool initialized;     /* True if subsystem initialized */
} coro_state = {0};

/* Stack size for each hart coroutine. The hart runs the whole interpreter and
 * JIT on this stack, so match the usual 8 MiB main-thread stack; pages are
 * committed lazily, so the unused part costs nothing.
 */
#define CORO_STACK_SIZE (8 * 1024 * 1024)

/* Inaccessible region below each stack. It is larger than one page so that a
 * single big frame cannot step over it into the neighboring mapping.
 */
#define CORO_GUARD_SIZE (64 * 1024)

/* Alternate signal stack, so the fault handler can still run once the coroutine
 * stack is exhausted.
 */
#define CORO_ALTSTACK_SIZE (64 * 1024)

/* Internal helper functions */

/* Thread-local variable for currently running coroutine */
static __thread coro_t *tls_running_coro = NULL;

static inline void coro_clear_running_state(void)
{
    tls_running_coro = NULL;
}

/* Forward declarations */

static void jump_out(coro_t *co);
static void coro_entry_wrapper(void *arg);

/* Context switch implementation */

#ifdef CORO_USE_ASM

/* Point the coroutine context at the top of its stack and its entry wrapper.
 * Used both on creation and when a restarted coroutine is resumed.
 */
static bool make_context(coro_t *co)
{
    coro_ctxbuf_t *ctx = &co->context.ctx;
    void *stack_base = co->stack_base;
    size_t stack_size = co->stack_size;

    /* Start from a clean register set: a restarted coroutine must not inherit
     * the frame pointer of the stack it abandoned.
     */
    memset(ctx, 0, sizeof(*ctx));
#if defined(__x86_64__)
    /* Reserve 128 bytes for Red Zone (System V AMD64 ABI) */
    stack_size = stack_size - 128;
    /* Ensure 16-byte alignment per ABI requirement */
    size_t stack_top = ((size_t) stack_base + stack_size) & ~15UL;
    void **stack_high_ptr = (void **) (stack_top - sizeof(size_t));
    stack_high_ptr[0] =
        (void *) (0xdeaddeaddeaddead); /* Dummy return address */
    ctx->rip = (void *) (_coro_wrap_main);
    ctx->rsp = (void *) (stack_high_ptr);
    ctx->r12 = (void *) (coro_entry_wrapper); /* Wrapper function pointer */
    ctx->r13 = (void *) (co);                 /* Coroutine pointer */
#elif defined(__aarch64__)
    /* Ensure 16-byte alignment per AAPCS64 requirement */
    size_t stack_top = ((size_t) stack_base + stack_size) & ~15UL;
    ctx->x[0] = (void *) (co); /* Coroutine pointer (x19) */
    ctx->x[1] =
        (void *) (coro_entry_wrapper); /* Wrapper function pointer (x20) */
    ctx->x[2] = (void *) (0xdeaddeaddeaddead); /* Dummy return address (x21) */
    ctx->sp = (void *) (stack_top);
    ctx->lr = (void *) (_coro_wrap_main);
#endif
    return true;
}

/* Jump into a coroutine */
static void jump_into(coro_t *co)
{
    coro_context_t *context = &co->context;
    tls_running_coro = co;
    _coro_switch(&context->back_ctx, &context->ctx);
}

/* Jump out of a coroutine */
static void jump_out(coro_t *co)
{
    coro_context_t *context = &co->context;
    coro_clear_running_state();
    _coro_switch(&context->ctx, &context->back_ctx);
}

#elif defined(CORO_USE_UCONTEXT)

/* Wrapper for ucontext entry point */
#if defined(_LP64) || defined(__LP64__)
static void wrap_main_ucontext(unsigned int lo, unsigned int hi)
{
    coro_entry_wrapper((void *) (((size_t) lo) | (((size_t) hi) << 32)));
}
#else
static void wrap_main_ucontext(unsigned int lo)
{
    coro_entry_wrapper((void *) ((size_t) lo));
}
#endif

/* Point the coroutine context at the top of its stack and its entry wrapper.
 * Used both on creation and when a restarted coroutine is resumed.
 */
static bool make_context(coro_t *co)
{
    coro_ctxbuf_t *ctx = &co->context.ctx;
    if (getcontext(ctx) != 0) {
        fprintf(stderr, "coro: failed to get ucontext\n");
        return false;
    }
    ctx->uc_link = NULL;
    ctx->uc_stack.ss_sp = co->stack_base;
    ctx->uc_stack.ss_size = co->stack_size;
    unsigned int lo = (unsigned int) ((size_t) co);
#if defined(_LP64) || defined(__LP64__)
    unsigned int hi = (unsigned int) (((size_t) co) >> 32);
    makecontext(ctx, (void (*)(void)) wrap_main_ucontext, 2, lo, hi);
#else
    makecontext(ctx, (void (*)(void)) wrap_main_ucontext, 1, lo);
#endif
    return true;
}

/* Jump into a coroutine */
static void jump_into(coro_t *co)
{
    coro_context_t *context = &co->context;
    tls_running_coro = co;
    swapcontext(&context->back_ctx, &context->ctx);
}

/* Jump out of a coroutine */
static void jump_out(coro_t *co)
{
    coro_context_t *context = &co->context;
    coro_clear_running_state();
    swapcontext(&context->ctx, &context->back_ctx);
}

#endif

/* Coroutine entry, reached from the _coro_wrap_main stub or the ucontext
 * wrapper. It must jump out rather than return: there is no caller frame on the
 * coroutine stack, and the ucontext has no uc_link.
 */
static void coro_entry_wrapper(void *arg)
{
    coro_t *co = (coro_t *) arg;
    co->func(co->user_data);
    co->state = CORO_STATE_DEAD;
    jump_out(co);
}

/* Map a coroutine stack with an inaccessible guard region below it, so an
 * overflow faults immediately instead of corrupting neighboring memory.
 */
static bool coro_alloc_stack(coro_t *co, size_t size)
{
    long page = sysconf(_SC_PAGESIZE);
    size_t align = page > 0 ? (size_t) page : 4096;
    size_t guard = (CORO_GUARD_SIZE + align - 1) & ~(align - 1);
    size = (size + align - 1) & ~(align - 1);

    void *map = mmap(NULL, size + guard, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED)
        return false;
    if (mprotect(map, guard, PROT_NONE) != 0) {
        munmap(map, size + guard);
        return false;
    }

    co->map_base = map;
    co->map_size = size + guard;
    co->stack_base = (uint8_t *) map + guard;
    co->stack_size = size;
    return true;
}

static void coro_free_stack(coro_t *co)
{
    if (co->map_base)
        munmap(co->map_base, co->map_size);
    co->map_base = NULL;
    co->stack_base = NULL;
}

/* Report coroutine stack overflows. The handler is stacked on top of whatever
 * was installed before coro_init() (the demand-paging handler in io.c) and
 * passes every other fault on to it.
 */
static struct sigaction prev_sigsegv_action, prev_sigbus_action;
static stack_t prev_altstack;
static void *altstack_mem;
static bool fault_handlers_installed;

static bool coro_is_guard_fault(uintptr_t addr)
{
    if (!coro_state.coroutines)
        return false;
    for (uint32_t i = 0; i < coro_state.total_slots; i++) {
        coro_t *co = coro_state.coroutines[i];
        if (co && co->map_base && addr >= (uintptr_t) co->map_base &&
            addr < (uintptr_t) co->stack_base)
            return true;
    }
    return false;
}

static void coro_fault_handler(int sig, siginfo_t *si, void *context)
{
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);

    if (coro_is_guard_fault((uintptr_t) si->si_addr)) {
        static const char msg[] = "FATAL: hart coroutine stack overflow\n";
        ssize_t ret = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void) ret;

        /* Returning re-executes the faulting access under the default action,
         * which terminates with the original fault state.
         */
        sigaction(sig, &dfl, NULL);
        return;
    }

    struct sigaction *prev =
        (sig == SIGSEGV) ? &prev_sigsegv_action : &prev_sigbus_action;
    if (prev->sa_flags & SA_SIGINFO) {
        prev->sa_sigaction(sig, si, context);
    } else if (prev->sa_handler == SIG_DFL) {
        sigaction(sig, &dfl, NULL);
        raise(sig);
    } else if (prev->sa_handler != SIG_IGN) {
        prev->sa_handler(sig);
    }
}

static void coro_install_fault_handlers(void)
{
    altstack_mem = malloc(CORO_ALTSTACK_SIZE);
    if (!altstack_mem)
        return;
    stack_t ss = {
        .ss_sp = altstack_mem,
        .ss_size = CORO_ALTSTACK_SIZE,
        .ss_flags = 0,
    };
    if (sigaltstack(&ss, &prev_altstack) != 0) {
        free(altstack_mem);
        altstack_mem = NULL;
        return;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = coro_fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, &prev_sigsegv_action) != 0) {
        sigaltstack(&prev_altstack, NULL);
        free(altstack_mem);
        altstack_mem = NULL;
        return;
    }
    if (sigaction(SIGBUS, &sa, &prev_sigbus_action) != 0) {
        sigaction(SIGSEGV, &prev_sigsegv_action, NULL);
        sigaltstack(&prev_altstack, NULL);
        free(altstack_mem);
        altstack_mem = NULL;
        return;
    }
    fault_handlers_installed = true;
}

static void coro_restore_fault_handlers(void)
{
    if (!fault_handlers_installed)
        return;
    sigaction(SIGBUS, &prev_sigbus_action, NULL);
    sigaction(SIGSEGV, &prev_sigsegv_action, NULL);
    sigaltstack(&prev_altstack, NULL);
    free(altstack_mem);
    altstack_mem = NULL;
    fault_handlers_installed = false;
}

/* Public API implementation */

bool coro_init(uint32_t total_slots)
{
    if (coro_state.initialized) {
        fprintf(stderr, "coro_init: already initialized\n");
        return false;
    }

    if (total_slots == 0 || total_slots > 256) {
        fprintf(stderr, "coro_init: invalid slots (total=%u)\n", total_slots);
        return false;
    }

    coro_state.coroutines = calloc(total_slots, sizeof(coro_t *));
    if (!coro_state.coroutines) {
        fprintf(stderr, "coro_init: failed to allocate coroutines array\n");
        return false;
    }

    coro_state.total_slots = total_slots;
    coro_state.initialized = true;

    /* Best effort: without the handler an overflow still faults on the guard
     * region, just without a diagnostic.
     */
    coro_install_fault_handlers();
    return true;
}

void coro_cleanup(void)
{
    if (!coro_state.initialized)
        return;

    coro_restore_fault_handlers();

    for (uint32_t i = 0; i < coro_state.total_slots; i++) {
        if (coro_state.coroutines[i]) {
            coro_t *co = coro_state.coroutines[i];
            coro_free_stack(co);
            free(co);
            coro_state.coroutines[i] = NULL;
        }
    }

    free(coro_state.coroutines);
    coro_state.coroutines = NULL;
    coro_state.total_slots = 0;
    coro_state.initialized = false;
    tls_running_coro = NULL; /* Reset TLS as well */
}

bool coro_create_hart(uint32_t slot_id, void (*func)(void *), void *arg)
{
    if (!coro_state.initialized) {
        fprintf(stderr, "coro_create_hart: not initialized\n");
        return false;
    }

    if (slot_id >= coro_state.total_slots) {
        fprintf(stderr, "coro_create_hart: invalid slot_id=%u\n", slot_id);
        return false;
    }

    if (!func) {
        fprintf(stderr, "coro_create_hart: func is NULL\n");
        return false;
    }

    if (coro_state.coroutines[slot_id]) {
        fprintf(stderr, "coro_create_hart: slot %u already has coroutine\n",
                slot_id);
        return false;
    }

    /* Allocate coroutine structure */
    coro_t *co = calloc(1, sizeof(coro_t));
    if (!co) {
        fprintf(stderr, "coro_create_hart: failed to allocate coroutine\n");
        return false;
    }

    /* Store user function and data */
    co->func = func;
    co->user_data = arg;
    co->state = CORO_STATE_SUSPENDED;

    /* Allocate stack */
    if (!coro_alloc_stack(co, CORO_STACK_SIZE)) {
        fprintf(stderr, "coro_create_hart: failed to allocate stack\n");
        free(co);
        return false;
    }

    if (!make_context(co)) {
        coro_free_stack(co);
        free(co);
        return false;
    }

    coro_state.coroutines[slot_id] = co;
    return true;
}

void coro_resume_hart(uint32_t slot_id)
{
    if (!coro_state.initialized || slot_id >= coro_state.total_slots) {
        fprintf(stderr, "coro_resume_hart: invalid slot_id=%u\n", slot_id);
        return;
    }

    coro_t *co = coro_state.coroutines[slot_id];
    if (!co) {
        fprintf(stderr, "coro_resume_hart: slot %u has no coroutine\n",
                slot_id);
        return;
    }

    if (co->state != CORO_STATE_SUSPENDED) {
        /* This may happen if a coroutine is waiting on I/O and the main loop
         * tries to resume it. It is not a fatal error.
         */
        return;
    }

    if (co->restart) {
        co->restart = false;
        if (!make_context(co))
            abort();
    }

    co->state = CORO_STATE_RUNNING;
    jump_into(co);
}

void coro_yield(void)
{
    if (!coro_state.initialized) {
        fprintf(stderr, "coro_yield: not initialized\n");
        return;
    }

    coro_t *co = tls_running_coro;
    if (!co) {
        fprintf(stderr, "coro_yield: no running coroutine\n");
        return;
    }

    if (co->state != CORO_STATE_RUNNING) {
        fprintf(stderr, "coro_yield: coroutine not running\n");
        return;
    }

    co->state = CORO_STATE_SUSPENDED;
    jump_out(co);
}

bool coro_restart_current(void)
{
    coro_t *co = tls_running_coro;
    if (!co || co->state != CORO_STATE_RUNNING)
        return false;

    co->restart = true;
    co->state = CORO_STATE_SUSPENDED;
    jump_out(co);
    return true;
}

#endif /* RV32_HAS_HART_CORO */
