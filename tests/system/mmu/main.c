/*
 * MMU Page Fault Test - User Space Component
 *
 * This code runs in user mode (U-mode) at virtual address 0x0. It tests
 * three types of page faults that the MMU must handle via demand paging:
 *
 * 1. Instruction Fetch Page Fault (scause=12): Triggered when main() starts
 *    executing, since user VA 0x0 has no valid PTE until the fault handler
 *    allocates a frame and copies code from the kernel mapping.
 *
 * 2. Load Page Fault (scause=13): Triggered by reading from VA 0x1000,
 *    which contains pf_str ("rv32emu"). The fault handler maps the page
 *    and copies data so subsequent reads succeed.
 *
 * 3. Store Page Fault (scause=15): Triggered by writing to VA 0x2000.
 *    The fault handler allocates a writable page and sets the Dirty bit.
 *
 * 4. Redirected Page Faults: loads from and a call to VA 0x3000, which the
 *    handler never maps; it resumes after the load, or at the return address,
 *    as a kernel does for an exception-table fixup or a signal. The loads
 *    repeat in a hot loop first, as code a JIT tier would compile.
 *
 * Memory Layout (defined in linker.ld):
 *   0x0000 - User code (.text.main) - this file
 *   0x1000 - User read-only data (.mystring) - pf_str
 *   0x2000 - User read-write data (.data.main, .bss.main)
 */

#include <stdint.h>

/* Place code/data in user VA sections (mapped by vm_boot's L2 page table) */
#define SECTION_TEXT_MAIN __attribute__((section(".text.main")))
#define SECTION_DATA_MAIN __attribute__((section(".data.main")))
#define SECTION_BSS_MAIN __attribute__((section(".bss.main")))

/* Write string to stdout via Linux write syscall (nr=64).
 * The ecall traps to the emulator's syscall handler in ELF_LOADER mode.
 */
#define printstr(ptr, length)          \
    do {                               \
        asm volatile(                  \
            "add a7, x0, 0x40;"        \
            "add a0, x0, 0x1;"         \
            "add a1, x0, %0;"          \
            "mv a2, %1;"               \
            "ecall;"                   \
            :                          \
            : "r"(ptr), "r"(length)    \
            : "a0", "a1", "a2", "a7"); \
    } while (0)

#define TEST_OUTPUT(msg, length) printstr(msg, length)

/* Copy string literal to stack, then print. Stack must be in physical memory
 * (not user VA) for emulator syscall handler compatibility.
 */
#define TEST_LOGGER(msg)                     \
    {                                        \
        char _msg[] = msg;                   \
        TEST_OUTPUT(_msg, sizeof(_msg) - 1); \
    }

#define SUCCESS 0
#define FAIL 1

/* Test string placed in .mystring section at VA 0x1000.
 * First read triggers load page fault; handler maps page with pf_str data.
 */
__attribute__((section(".mystring"))) const char pf_str[] = "rv32emu";

extern void _exit(int status);

static uint32_t load_words(const volatile uint32_t *p, uint32_t n);

/* main() must come first in .text.main: user_entry jumps to VA 0x0. */
int SECTION_TEXT_MAIN main()
{
    /* TEST 1: Instruction Fetch Page Fault
     *
     * Reaching this point means the instruction fetch page fault was handled.
     * When user_entry jumped to VA 0x0, the MMU found no valid PTE in user_l1pt
     * and raised scause=12. The trap handler allocated a frame, copied main()
     * from the kernel's direct mapping, and resumed execution here.
     */
    int x = 100;
    int y = 200;
    int z = x + y;
    (void) z;
    TEST_LOGGER("Instruction fetch page fault test passed!\n");

    /* TEST 2: Load Page Fault
     *
     * Read from VA 0x1000 where pf_str ("rv32emu") is located. The first load
     * triggers scause=13 since the page is unmapped. After the fault handler
     * maps it, subsequent loads from the same page succeed without faulting.
     */
    char buf[8];
    for (int i = 0; i < 8; i++)
        buf[i] = 0;

    volatile char *qtr = (char *) 0x1000;
    for (int i = 0; i < 8; i++)
        buf[i] = *(qtr + i);

    /* Verify data integrity: re-read should return same values (no fault) */
    for (int i = 0; i < 8; i++) {
        if (buf[i] != *(qtr + i)) {
            TEST_LOGGER("[Load page fault test] rv32emu string not match\n")
            _exit(FAIL);
        }
    }
    TEST_LOGGER("Load page fault test passed!\n");

    /* TEST 3: Store Page Fault
     *
     * Write to VA 0x2000, an unmapped writable page. The first store triggers
     * scause=15. The fault handler allocates a frame with PTE_D (dirty) set,
     * then subsequent stores and loads succeed without faulting.
     */
    for (int i = 0; i < 8; i++)
        buf[i] = 0;

    volatile char *ptr = (char *) 0x2000;
    *(ptr + 0) = 'r';
    *(ptr + 1) = 'v';
    *(ptr + 2) = '3';
    *(ptr + 3) = '2';
    *(ptr + 4) = 'e';
    *(ptr + 5) = 'm';
    *(ptr + 6) = 'u';
    *(ptr + 7) = '\0';

    /* Read back written data to verify store succeeded */
    for (int i = 0; i < 8; i++)
        buf[i] = *(ptr + i);

    /* Verify data integrity: what we wrote should match what we read */
    for (int i = 0; i < 8; i++) {
        if (buf[i] != *(ptr + i)) {
            TEST_LOGGER("[Store page fault test] rv32emu string not match\n")
            _exit(FAIL);
        }
    }
    TEST_LOGGER("Store page fault test passed!\n");

    /* TEST 4: Redirected Page Faults
     *
     * Call load_words() often enough on a mapped page for its code to become
     * hot, then on VA 0x3000. Every load from there must fault once and be
     * skipped, and nothing else may be skipped with it. A call into VA 0x3000
     * must fault once on its first fetch and return.
     */
    for (int i = 0; i < 200000; i++) {
        if (load_words((const volatile uint32_t *) 0x2000, 16)) {
            TEST_LOGGER("[Redirected page fault test] mapped load skipped\n")
            _exit(FAIL);
        }
    }
    if (load_words((const volatile uint32_t *) 0x3000, 64) != 64) {
        TEST_LOGGER("[Redirected page fault test] wrong number of skips\n")
        _exit(FAIL);
    }
    register uint32_t skipped asm("t6") = 0;
    asm volatile("jalr ra, 0(%1)"
                 : "+r"(skipped)
                 : "r"(0x3000)
                 : "ra", "memory");
    if (skipped != 1) {
        TEST_LOGGER("[Redirected page fault test] call not returned\n")
        _exit(FAIL);
    }
    TEST_LOGGER("Redirected page fault test passed!\n");

    _exit(SUCCESS);
}

/* Load n words from p, alternating between its first two words, and return how
 * many loads the kernel skipped: its handler counts them in t6.
 */
__attribute__((noinline)) static uint32_t SECTION_TEXT_MAIN
load_words(const volatile uint32_t *p, uint32_t n)
{
    register uint32_t skipped asm("t6") = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v;
        asm volatile("lw %0, 0(%2)"
                     : "=r"(v), "+r"(skipped)
                     : "r"(p + (i & 1))
                     : "memory");
        (void) v;
    }
    return skipped;
}
