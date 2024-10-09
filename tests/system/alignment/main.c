#define write_csr(reg, val) ({ asm volatile("csrw " #reg ", %0" ::"rK"(val)); })

#define printstr(ptr, length)                   \
    do {                                        \
        asm volatile(                           \
            "add a7, x0, 0x40;"                 \
            "add a0, x0, 0x1;" /* stdout */     \
            "add a1, x0, %0;"                   \
            "mv a2, %1;" /* length character */ \
            "ecall;"                            \
            :                                   \
            : "r"(ptr), "r"(length)             \
            : "a0", "a1", "a2", "a7");          \
    } while (0)

#define TEST_OUTPUT(msg, length) printstr(msg, length)

#define TEST_LOGGER(msg)                     \
    {                                        \
        char _msg[] = msg;                   \
        TEST_OUTPUT(_msg, sizeof(_msg) - 1); \
    }

extern int *misalign_data;

extern void misalign_func();

extern void misalign_trap_handler();

int main()
{
    /* init S-Mode trap handler */
    write_csr(stvec, misalign_trap_handler);

    /* misalign load */
    const int x = *misalign_data;
    /*
     * executing the registered trap handler is regarded as a pass.
     * (use gdb to track)
     */
    TEST_LOGGER("MISALIGNED LOAD TEST PASSED!\n");

    /* misalign store */
    char *ptr = (char *) misalign_data;
    *(int *) (ptr + 3) = x + 3;
    /*
     * executing the registered trap handler is regarded as a pass.
     * (use gdb to track)
     */
    TEST_LOGGER("MISALIGNED STORE TEST PASSED!\n");

    /*
     * misalign instuction fetch
     *
     * ENABLE_EXT_C must be disabled when building rv32emu before running this
     * test, as the jalr instruction only checks for misalignment when
     * compressed instruction support is not enabled
     *
     */
    misalign_func();
    /*
     * executing the registered trap handler is regarded as a pass.
     * (use gdb to track)
     */
    TEST_LOGGER("MISALIGNED INSTRUCTION FETCH TEST PASSED!\n");

    return 0;
}
