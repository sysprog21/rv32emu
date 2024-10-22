#define SECTION_TEXT_MAIN __attribute__((section(".text.main")))
#define SECTION_DATA_MAIN __attribute__((section(".data.main")))
#define SECTION_BSS_MAIN __attribute__((section(".bss.main")))

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

#define SUCCESS 0
#define FAIL 1

__attribute__((section(".mystring"))) const char pagefault_load_str[] =
    "rv32emu";

extern void _exit(int status);

int SECTION_TEXT_MAIN main()
{
    /* instruction fetch page fault test */
    int x = 100; /* trigger instruction page fault */
    int y = 200;
    int z = x + y;
    TEST_LOGGER("INSTRUCTION FETCH PAGE FAULT TEST PASSED!\n");

    char buf[8];
    /* data load page fault test */
    /* Clear buffer */
    for (int i = 0; i < 8; i++) {
        buf[i] = 0;
    }

    char *qtr = (char *) 0x1000; /* first data page */
    /* should trigger load page fault and load pagefault_load_str */
    for (int i = 0; i < 8; i++) {
        qtr = (char *) 0x1000; /* FIXME: weird result without this */
        buf[i] = *(qtr + i);
    }

    for (int i = 0; i < 8; i++) { /* should not trigger load page fault */
        if (buf[i] != *(qtr + i)) {
            TEST_LOGGER("[LOAD PAGE FAULT TEST] rv32emu string not match\n")
            _exit(FAIL);
        }
    }
    TEST_LOGGER("LOAD PAGE FAULT TEST PASSED!\n");

    /* data store page fault test */
    /* Clear buffer */
    for (int i = 0; i < 8; i++) {
        buf[i] = 0;
    }

    char *ptr = (char *) 0x2000; /* second data page */
    *ptr = 'r';                  /* trigger store page fault */
    ptr = (char *) 0x2000;       /* FIXME: weird result without this */
    *(ptr + 1) = 'v';            /* should not trigger store page fault */
    ptr = (char *) 0x2000;       /* FIXME: weird result without this */
    *(ptr + 2) = '3';            /* should not trigger store page fault */
    ptr = (char *) 0x2000;       /* FIXME: weird result without this */
    *(ptr + 3) = '2';            /* should not trigger store page fault */
    ptr = (char *) 0x2000;       /* FIXME: weird result without this */
    *(ptr + 4) = 'e';            /* should not trigger store page fault */
    ptr = (char *) 0x2000;       /* FIXME: weird result without this */
    *(ptr + 5) = 'm';            /* should not trigger store page fault */
    ptr = (char *) 0x2000;       /* FIXME: weird result without this */
    *(ptr + 6) = 'u';            /* should not trigger store page fault */
    ptr = (char *) 0x2000;       /* FIXME: weird result without this */
    *(ptr + 7) = '\0';           /* should not trigger store page fault */

    /* should not trigger load page fault */
    for (int i = 0; i < 8; i++) {
        buf[i] = *(ptr + i);
    }

    /* should not trigger load page fault */
    for (int i = 0; i < 8; i++) {
        if (buf[i] != *(ptr + i)) {
            TEST_LOGGER("[STORE PAGE FAULT TEST] rv32emu string not match\n")
            _exit(FAIL);
        }
    }
    TEST_LOGGER("STORE PAGE FAULT TEST PASSED!\n");

    _exit(SUCCESS);
}
