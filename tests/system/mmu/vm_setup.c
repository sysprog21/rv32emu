/*
 * MMU Page Fault Test - Supervisor/Kernel Component
 *
 * This file implements the S-mode (supervisor) trap handler and virtual memory
 * setup for testing rv32emu's MMU page fault handling. It runs at VA 0x80000000
 * (mapped from PA 0x00800000) and handles page faults from user code at VA 0x0.
 *
 * Key components:
 *   - vm_boot(): Sets up Sv32 page tables, enables paging, delegates faults
 *   - handle_trap(): Dispatches exceptions to appropriate handlers
 *   - handle_fault(): Demand paging - allocates frames and populates PTEs
 *
 * Page Table Structure (Sv32, 2-level):
 *   l1pt[0]    -> user_l1pt (L2 table for user VA 0x00000000-0x003FFFFF)
 *   l1pt[2]    -> Identity map PA 0x00800000 (allows paging enable w/o
 * trampoline) l1pt[512]  -> Kernel megapage at VA 0x80000000 l1pt[1023] ->
 * Kernel direct map for copying user pages on fault
 *
 * Reference:
 *   1. https://github.com/sifive/example-vm-test
 *   2. https://github.com/yutongshen/RISC-V-Simulator
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if (!defined(__GNUC__) && !defined(__clang__)) || !defined(__riscv) || \
    32 != __riscv_xlen
#error "GCC or Clang toolchain for 32-bit RISC-V is required"
#endif

/* Place supervisor code/data in kernel sections at PA 0x00800000 */
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

/* CSR access macros using RISC-V Zicsr instructions */
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

/* Sv32 paging mode constants */
#define SV32_MODE 0x80000000    /* SATP.MODE = Sv32 (bit 31) */
#define BARE_MODE 0x00000000    /* SATP.MODE = Bare (no translation) */
#define PG_SHIFT 12             /* 4 KiB page size = 2^12 */
#define PG_SIZE (1U << 12)      /* 4096 bytes per page */
#define MEGA_PG (PG_SIZE << 10) /* 4 MiB megapage (1024 * 4 KiB) */

/* Physical frame allocator: 32 pages starting at 4 MiB for demand paging */
#define FREE_FRAME_BASE 0x400000
#define MAX_TEST_PG 32

/* Exception cause codes (scause register values per RISC-V privilege spec) */
#define CAUSE_USER_ECALL 8
#define CAUSE_SUPERVISOR_ECALL 9
#define CAUSE_FETCH_PAGE_FAULT 12 /* Instruction fetch from unmapped page */
#define CAUSE_LOAD_PAGE_FAULT 13  /* Load from unmapped/non-readable page */
#define CAUSE_STORE_PAGE_FAULT 15 /* Store to unmapped/non-writable page */

/* sstatus.SUM: Supervisor User Memory access (allows S-mode to access U pages)
 */
#define SSTATUS_SUM (1U << 18)

/* Sv32 Page Table Entry (PTE) flag bits */
#define PTE_V (1U)      /* Valid: PTE contains a valid translation */
#define PTE_R (1U << 1) /* Readable: page can be read */
#define PTE_W (1U << 2) /* Writable: page can be written */
#define PTE_X (1U << 3) /* Executable: page can be executed */
#define PTE_U (1U << 4) /* User: page accessible in U-mode */
#define PTE_G (1U << 5) /* Global: mapping exists in all address spaces */
#define PTE_A (1U << 6) /* Accessed: page has been read/executed */
#define PTE_D (1U << 7) /* Dirty: page has been written */

/* External symbols from setup.S and linker script */
extern void main();                  /* User code entry at VA 0x0 */
extern void _start();                /* Kernel entry at PA 0x00800000 */
extern int user_entry();             /* Trampoline to jump to user space */
extern void supervisor_trap_entry(); /* S-mode trap vector (saves context) */
extern void pop_tf(trapframe_t *);   /* Restore context and sret */
extern void _exit(int status);       /* Exit via ecall (syscall 93) */

typedef uint32_t pte_t;

/* Simple linked-list frame allocator for demand paging */
typedef struct {
    pte_t addr; /* Physical address of this free frame */
    void *next; /* Next node in freelist (NULL if last) */
} freelist_t;

freelist_t freelist_nodes[MAX_TEST_PG] SECTION_BSS_VMSETUP;
freelist_t *SECTION_BSS_VMSETUP freelist_head;
freelist_t *SECTION_BSS_VMSETUP freelist_tail;

/* Sv32 page table layout:
 *   l1pt (pt[0]): Root L1 page table, pointed to by SATP
 *   user_l1pt (pt[1]): L2 table for user VA 0x0-0x3FFFFF (4 MiB)
 */
#define l1pt pt[0]
#define user_l1pt pt[1]
#define NPT 2
#define PTES_PER_PT (1U << 10) /* 1024 entries per page table */
#define PTE_PPN_SHIFT 10       /* PPN starts at bit 10 in PTE */
pte_t pt[NPT][PTES_PER_PT] SECTION_BSS_VMSETUP
    __attribute__((aligned(PG_SIZE)));

#define MASK(n) (~((~0U << (n))))

/* Address translation helpers for kernel access to user pages.
 *
 * The kernel megapage at l1pt[1023] maps PA starting at main() to VA
 * 0xFFC00000. For user VA X, the corresponding kernel VA is X - MEGA_PG
 * (0x400000).
 *
 * Example: User VA 0x1000 -> 0x1000 - 0x400000 = 0xFFC01000 (kernel VA)
 *
 * Note: The subtraction underflows for small values, but unsigned arithmetic
 * wraparound produces the correct high address (e.g., 0 - 0x400000 =
 * 0xFFC00000).
 */
#define pa2kva(x) (((pte_t) (x)) - ((pte_t) (_start)) - MEGA_PG)
#define uva2kva(x) (((pte_t) (x)) - MEGA_PG)

void *SECTION_TEXT_VMSETUP memcpy(void *ptr, void *src, size_t len)
{
    uint32_t *word_ptr = ptr, *word_src = src;

    while (len >= 4) {
        *word_ptr++ = *word_src++;
        len -= 4;
    }

    char *byte_ptr = (char *) word_ptr, *byte_src = (char *) word_src;
    while (len--)
        *byte_ptr++ = *byte_src++;

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
    static char digitals[16] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
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

/* Demand paging: allocate a physical frame and map it to the faulting user VA.
 *
 * This implements a simple demand-paging scheme:
 * 1. Round faulting address down to page boundary
 * 2. Allocate a free physical frame from the freelist
 * 3. Create PTE with appropriate permissions (RWX for user)
 * 4. Flush TLB entry for the faulting address
 * 5. Copy page contents from kernel's direct mapping of user memory
 *
 * The copy step populates the new page with data from the ELF sections
 * (code from .text.main, data from .mystring, etc.) that were loaded at
 * the corresponding physical addresses.
 */
void SECTION_TEXT_VMSETUP handle_fault(uint32_t addr, uint32_t cause)
{
    /* Page-align the faulting address */
    addr = addr >> PG_SHIFT << PG_SHIFT;

    /* Locate PTE in user L2 table: extract VPN[0] (bits 12-21) */
    pte_t *pte = user_l1pt + ((addr >> PG_SHIFT) & MASK(10));

    /* Allocate physical frame from freelist */
    assert(freelist_head);
    pte_t pa = freelist_head->addr;
    freelist_head = freelist_head->next;

    /* Build PTE: PPN | flags. All user pages get RWXU permissions.
     * Set Accessed bit; set Dirty bit only for store faults. */
    *pte = (pa >> PG_SHIFT << PTE_PPN_SHIFT) | PTE_A | PTE_U | PTE_R | PTE_W |
           PTE_X | PTE_V;
    if (cause == CAUSE_STORE_PAGE_FAULT)
        *pte |= PTE_D;

    /* Invalidate stale TLB entry before accessing the new mapping */
    asm volatile("sfence.vma %0" ::"r"(addr) : "memory");

    /* Copy page contents from kernel's view of user memory.
     * Enable SUM (Supervisor User Memory access) temporarily.
     */
    set_csr(sstatus, SSTATUS_SUM);
    memcpy((uint32_t *) addr, (uint32_t *) uva2kva(addr), PG_SIZE);
    clear_csr(sstatus, SSTATUS_SUM);
}

/* S-mode trap handler entry point (called from supervisor_trap_entry in
 * setup.S).
 *
 * Dispatches exceptions based on scause. For this test, only page faults are
 * expected. After handling, restores user context via pop_tf() which executes
 * sret to return to user mode at the faulting instruction (now mapped).
 */
void SECTION_TEXT_VMSETUP handle_trap(trapframe_t *tf)
{
    if (tf->cause == CAUSE_FETCH_PAGE_FAULT ||
        tf->cause == CAUSE_LOAD_PAGE_FAULT ||
        tf->cause == CAUSE_STORE_PAGE_FAULT) {
        handle_fault(tf->badvaddr, tf->cause);
    } else {
        assert(!"Unknown exception");
    }

    /* Return to user mode: restore registers and sret */
    pop_tf(tf);
}

/* Initialize Sv32 page tables, enable paging, and jump to user mode.
 *
 * This is called from _start after basic register initialization. It sets up
 * the two-level Sv32 page table structure, configures trap delegation, and
 * enters user mode via sret.
 */
void SECTION_TEXT_VMSETUP vm_boot()
{
    /* L1[0]: Point to L2 table for user VA 0x00000000-0x003FFFFF.
     * User pages are initially unmapped; PTEs populated on demand by faults.
     */
    l1pt[0] = ((pte_t) user_l1pt >> PG_SHIFT << PTE_PPN_SHIFT) | PTE_V;

    /* L1[1023]: Kernel direct map at VA 0xFFC00000 for accessing user memory.
     * Maps PA of main() so kernel can copy user pages during fault handling.
     * The uva2kva() macro relies on this mapping: uva - 0x400000 = kva.
     * PTE_G marks this as global (not flushed on ASID change).
     */
    l1pt[PTES_PER_PT - 1] = ((pte_t) main >> PG_SHIFT << PTE_PPN_SHIFT) |
                            PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A |
                            PTE_D;

    /* L1[512]: Kernel megapage at VA 0x80000000, mapping PA 0x00800000.
     * All kernel code runs at this VA after paging is enabled.
     * PTE_G marks this as global (not flushed on ASID change).
     */
    l1pt[PTES_PER_PT >> 1] = ((pte_t) _start >> PG_SHIFT << PTE_PPN_SHIFT) |
                             PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A |
                             PTE_D;

    /* L1[2]: Identity map PA 0x00800000 to VA 0x00800000.
     * Required so the PC (still at physical address) remains valid immediately
     * after SATP write enables paging. Without this, the next instruction
     * fetch would fault before we can jump to kernel VA 0x80000000.
     * PTE_G marks this as global (not flushed on ASID change).
     */
    l1pt[2] = ((pte_t) _start >> PG_SHIFT << PTE_PPN_SHIFT) | PTE_V | PTE_R |
              PTE_W | PTE_X | PTE_G | PTE_A | PTE_D;

    /* Enable Sv32 paging by writing SATP with mode=1 and PPN of root table */
    uintptr_t satp_val = ((pte_t) &l1pt >> PG_SHIFT) | SV32_MODE;
    write_csr(satp, satp_val);

    /* Flush all TLB entries to ensure new translations take effect */
    asm volatile("sfence.vma" ::: "memory");

    /* Configure S-mode trap handling:
     * - stvec: trap vector points to supervisor_trap_entry (in setup.S)
     * - sscratch: holds trapframe pointer for register save/restore
     */
    write_csr(stvec, supervisor_trap_entry);
    write_csr(sscratch, read_csr(mscratch));

    /* Delegate page fault exceptions from M-mode to S-mode.
     * medeleg uses bit positions: bit N delegates exception code N. */
    write_csr(medeleg, (1U << CAUSE_FETCH_PAGE_FAULT) |
                           (1U << CAUSE_LOAD_PAGE_FAULT) |
                           (1U << CAUSE_STORE_PAGE_FAULT));

    /* Initialize frame allocator: linked list of 32 free 4KB frames */
    freelist_head = (void *) &freelist_nodes[0];
    freelist_tail = &freelist_nodes[MAX_TEST_PG - 1];
    for (uint32_t i = 0; i < MAX_TEST_PG; i++) {
        freelist_nodes[i].addr = FREE_FRAME_BASE + i * PG_SIZE;
        freelist_nodes[i].next = &freelist_nodes[i + 1];
    }
    freelist_nodes[MAX_TEST_PG - 1].next = 0;

    /* Enter user mode: set up minimal trapframe and sret to user_entry,
     * which will set up user stack and jump to VA 0x0 (main).
     */
    trapframe_t tf;
    memset(&tf, 0, sizeof(tf));
    tf.epc = (uint32_t) &user_entry;
    pop_tf(&tf);
}
