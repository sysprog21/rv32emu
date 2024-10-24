/*
 * Reference:
 * 1. https://github.com/sifive/example-vm-test
 * 2. https://github.com/yutongshen/RISC-V-Simulator
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(__GNUC__) || !defined(__riscv) || 32 != __riscv_xlen
#error "GNU Toolchain for 32-bit RISC-V is required"
#endif

#define SECTION_TEXT_VMSETUP __attribute__((section(".text.vm_setup")))
#define SECTION_DATA_VMSETUP __attribute__((section(".data.vm_setup")))
#define SECTION_BSS_VMSETUP __attribute__((section(".bss.vm_setup")))

#define assert(x)                                                     \
    do {                                                              \
        if (!x) {                                                     \
            printf("Assertion failed '%s' at line %d of '%s'\n", #x); \
            _exit(1);                                                 \
        }                                                             \
    } while (0)

#define read_csr(reg)                                 \
    ({                                                \
        uint32_t __tmp;                               \
        asm volatile("csrr %0, " #reg : "=r"(__tmp)); \
        __tmp;                                        \
    })

#define write_csr(reg, val) ({ asm volatile("csrw " #reg ", %0" ::"rK"(val)); })

#define swap_csr(reg, val)                                                \
    ({                                                                    \
        uint32_t __tmp;                                                   \
        asm volatile("csrrw %0, " #reg ", %1" : "=r"(__tmp) : "rK"(val)); \
        __tmp;                                                            \
    })

#define set_csr(reg, bit)                                                 \
    ({                                                                    \
        uint32_t __tmp;                                                   \
        asm volatile("csrrs %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit)); \
        __tmp;                                                            \
    })

#define clear_csr(reg, bit)                                               \
    ({                                                                    \
        uint32_t __tmp;                                                   \
        asm volatile("csrrc %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit)); \
        __tmp;                                                            \
    })

typedef struct {
    uint32_t ra;
    uint32_t sp;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t s0;
    uint32_t s1;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t status;
    uint32_t epc;
    uint32_t badvaddr;
    uint32_t cause;
} trapframe_t;

#define SV32_MODE 0x80000000
#define BARE_MODE 0x00000000
#define PG_SHIFT 12
#define PG_SIZE (1U << 12)
#define FREE_FRAME_BASE 0x400000
#define MAX_TEST_PG 32          /* FREE_FRAME_BASE - 0x41ffff */
#define MEGA_PG (PG_SIZE << 10) /* 4 MiB */

#define CAUSE_USER_ECALL (1U << 8)
#define CAUSE_SUPERVISOR_ECALL (1U << 9)
#define CAUSE_FETCH_PAGE_FAULT (1U << 12)
#define CAUSE_LOAD_PAGE_FAULT (1U << 13)
#define CAUSE_STORE_PAGE_FAULT (1U << 15)

#define SSTATUS_SUM (1U << 18)

#define PTE_V (1U)
#define PTE_R (1U << 1)
#define PTE_W (1U << 2)
#define PTE_X (1U << 3)
#define PTE_U (1U << 4)
#define PTE_G (1U << 5)
#define PTE_A (1U << 6)
#define PTE_D (1U << 7)

extern void main();
extern void _start();
extern int user_entry();
extern void supervisor_trap_entry();
extern void pop_tf(trapframe_t *);
extern void _exit(int status);

typedef uint32_t pte_t;
typedef struct {
    pte_t addr;
    void *next;
} freelist_t;

freelist_t freelist_nodes[MAX_TEST_PG] SECTION_BSS_VMSETUP;
freelist_t *SECTION_BSS_VMSETUP freelist_head;
freelist_t *SECTION_BSS_VMSETUP freelist_tail;

#define l1pt pt[0]
#define user_l1pt pt[1]
#define NPT 2
#define PTES_PER_PT (1U << 10)
#define PTE_PPN_SHIFT 10
pte_t pt[NPT][PTES_PER_PT] SECTION_BSS_VMSETUP
    __attribute__((aligned(PG_SIZE)));

#define MASK(n) (~((~0U << (n))))
#define pa2kva(x) (((pte_t) (x)) - ((pte_t) (_start)) - MEGA_PG)
#define uva2kva(x) (((pte_t) (x)) - MEGA_PG)

void *SECTION_TEXT_VMSETUP memcpy(void *ptr, void *src, size_t len)
{
    uint32_t *word_ptr = ptr;
    uint32_t *word_src = src;

    while (len >= 4) {
        *word_ptr++ = *word_src++;
        len -= 4;
    }

    char *byte_ptr = (char *) word_ptr;
    char *byte_src = (char *) word_src;
    while (len--) {
        *byte_ptr++ = *byte_src++;
    }

    return ptr;
}

void *SECTION_TEXT_VMSETUP memset(void *ptr, int val, size_t len)
{
    uint32_t *word_ptr = ptr;
    uint32_t write_word = (char) val;
    write_word |= write_word << 8;
    write_word |= write_word << 16;

    while (len >= 4) {
        *word_ptr++ = write_word;
        len -= 4;
    }

    char *byte_ptr = (char *) word_ptr;
    while (len--) {
        *byte_ptr++ = val;
    }

    return ptr;
}

#define SYSCALL_WRITE 64
int SECTION_TEXT_VMSETUP putchar(int ch)
{
    int syscall_nr = SYSCALL_WRITE;
    asm volatile(
        "mv a7, %0;"
        "li a0, 0x1;" /* stdout */
        "add a1, x0, %1;"
        "li a2, 0x1;" /* one character */
        "ecall;"
        :
        : "r"(syscall_nr), "r"(&ch)
        : "a0", "a1", "a2", "a7");

    return 0;
}

int SECTION_TEXT_VMSETUP _puts(const char *s)
{
    int res = 0;
    while (*s) {
        putchar(*(s++));
        ++res;
    }

    return res;
}

int SECTION_TEXT_VMSETUP puts(const char *s)
{
    int res = 1;
    res += _puts(s);
    putchar('\n');

    return res;
}

char *SECTION_TEXT_VMSETUP itoa(uint32_t value,
                                int base,
                                int min_len,
                                char fill_char)
{
    static char digitals[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    static char str[64];
    char *ptr = str + 63;
    int tmp;
    *ptr = 0;
    do {
        if (!value)
            *--ptr = fill_char;
        else
            *--ptr = digitals[value % base];
        value /= base;
    } while (--min_len > 0 || value);

    return ptr;
}

int SECTION_TEXT_VMSETUP printf(const char *format, ...)
{
    const char *ptr = format;
    int res = 0;

    va_list va;
    va_start(va, format);

    while (*ptr) {
        if (*ptr == '%') {
            int min_len = 0;
            char fill_char = 0;
        loop1:
            switch (*(++ptr)) {
            case 'c':
                assert(!(min_len || fill_char));
                ++res;
                putchar(va_arg(va, int));
                break;
            case 's':
                assert(!(min_len || fill_char));
                res += _puts(va_arg(va, const char *));
                break;
            case 'x': {
                assert(!(!fill_char ^ !min_len));
                uint32_t n = va_arg(va, uint32_t);
                res += _puts(itoa(n, 16, min_len, min_len ? fill_char : '0'));
            } break;
            case 'd': {
                assert(!(!fill_char ^ !min_len));
                int64_t n = va_arg(va, int64_t);
                if (n < 0) {
                    ++res;
                    putchar('-');
                    n = -n;
                }
                res += _puts(itoa(n, 10, min_len, min_len ? fill_char : '0'));
            } break;
            case '%':
                ++res;
                putchar('%');
                break;
            case '1':
                assert(fill_char);
                min_len *= 10;
                min_len += 1;
                goto loop1;
            case '2':
                assert(fill_char);
                min_len *= 10;
                min_len += 2;
                goto loop1;
            case '3':
                assert(fill_char);
                min_len *= 10;
                min_len += 3;
                goto loop1;
            case '4':
                assert(fill_char);
                min_len *= 10;
                min_len += 4;
                goto loop1;
            case '5':
                assert(fill_char);
                min_len *= 10;
                min_len += 5;
                goto loop1;
            case '6':
                assert(fill_char);
                min_len *= 10;
                min_len += 6;
                goto loop1;
            case '7':
                assert(fill_char);
                min_len *= 10;
                min_len += 7;
                goto loop1;
            case '8':
                assert(fill_char);
                min_len *= 10;
                min_len += 8;
                goto loop1;
            case '9':
                assert(fill_char);
                min_len *= 10;
                min_len += 9;
                goto loop1;
            default:
                assert(!min_len);
                fill_char = *ptr;
                goto loop1;
            }
        } else {
            ++res;
            putchar(*ptr);
        }
        ++ptr;
    }
    return res;
}

void SECTION_TEXT_VMSETUP handle_fault(uint32_t addr, uint32_t cause)
{
    addr = addr >> PG_SHIFT << PG_SHIFT; /* round down page */
    pte_t *pte = user_l1pt + ((addr >> PG_SHIFT) & MASK(10));

    /* create a new pte */
    assert(freelist_head);
    pte_t pa = freelist_head->addr;
    freelist_head = freelist_head->next;
    *pte = (pa >> PG_SHIFT << PTE_PPN_SHIFT) | PTE_A | PTE_U | PTE_R | PTE_W |
           PTE_X | PTE_V;
    if ((1U << cause) == CAUSE_STORE_PAGE_FAULT)
        *pte |= PTE_D;

    /* temporarily allow kernel to access user memory to copy data */
    set_csr(sstatus, SSTATUS_SUM);
    /* page table is updated, so main should not cause trap here */
    memcpy((uint32_t *) addr, (uint32_t *) uva2kva(addr), PG_SIZE);
    /* disallow kernel to access user memory */
    clear_csr(sstatus, SSTATUS_SUM);
}

void SECTION_TEXT_VMSETUP handle_trap(trapframe_t *tf)
{
    if ((1U << tf->cause) == CAUSE_FETCH_PAGE_FAULT ||
        (1U << tf->cause) == CAUSE_LOAD_PAGE_FAULT ||
        (1U << tf->cause) == CAUSE_STORE_PAGE_FAULT) {
        handle_fault(tf->badvaddr, tf->cause);
    } else
        assert(!"Unknown exception");

    pop_tf(tf);
}

void SECTION_TEXT_VMSETUP vm_boot()
{
    /* map first page table entry to a next level page table for user */
    l1pt[0] = ((pte_t) user_l1pt >> PG_SHIFT << PTE_PPN_SHIFT) | PTE_V;
    /* map last page table leaf entry for kernel which direct maps for user
     * virtual memory, note that this is a trick of 2's complement, e.g., 0 -
     * MEGA_PG = 0b1111111111xxx...x when page fault occurs after entering main
     */
    l1pt[PTES_PER_PT - 1] = ((pte_t) main >> PG_SHIFT << PTE_PPN_SHIFT) |
                            PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;

    /* direct map kernel virtual memory */
    l1pt[PTES_PER_PT >> 1] = ((pte_t) _start >> PG_SHIFT << PTE_PPN_SHIFT) |
                             PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;

    /* Enable paging */
    uintptr_t satp_val = ((pte_t) &l1pt >> PG_SHIFT) | SV32_MODE;
    write_csr(satp, satp_val);

    /* set up supervisor trap handler */
    write_csr(stvec, supervisor_trap_entry);
    write_csr(sscratch, read_csr(mscratch));
    /* No delegation */
    /*write_csr(medeleg, CAUSE_FETCH_PAGE_FAULT | CAUSE_LOAD_PAGE_FAULT |
                           CAUSE_STORE_PAGE_FAULT);*/

    freelist_head = (void *) &freelist_nodes[0];
    freelist_tail = &freelist_nodes[MAX_TEST_PG - 1];
    for (uint32_t i = 0; i < MAX_TEST_PG; i++) {
        freelist_nodes[i].addr = FREE_FRAME_BASE + i * PG_SIZE;
        freelist_nodes[i].next = &freelist_nodes[i + 1];
    }
    freelist_nodes[MAX_TEST_PG - 1].next = 0;

    trapframe_t tf;
    memset(&tf, 0, sizeof(tf));
    tf.epc = (uint32_t) &user_entry;
    pop_tf(&tf);
}
