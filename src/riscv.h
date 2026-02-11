/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "io.h"
#include "log.h"
#include "map.h"

#if RV32_HAS(SYSTEM)
#include "devices/plic.h"
#if RV32_HAS(GOLDFISH_RTC)
#include "devices/rtc.h"
#endif /* RV32_HAS(GOLDFISH_RTC) */
#include "devices/uart.h"
#include "devices/virtio.h"
#endif /* RV32_HAS(SYSTEM) */

#if RV32_HAS(EXT_F)
#define float16_t softfloat_float16_t
#define bfloat16_t softfloat_bfloat16_t
#define float32_t softfloat_float32_t
#define float64_t softfloat_float64_t
#include "softfloat/source/include/softfloat.h"
#undef float16_t
#undef bfloat16_t
#undef float32_t
#undef float64_t
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* clang-format off */
#define RV_REGS_LIST                                   \
    _(zero) /* hard-wired zero, ignoring any writes */ \
    _(ra)   /* return address */                       \
    _(sp)   /* stack pointer */                        \
    _(gp)   /* global pointer */                       \
    _(tp)   /* thread pointer */                       \
    _(t0)   /* temporary/alternate link register */    \
    _(t1)   /* temporaries */                          \
    _(t2)                                              \
    _(s0) /* saved register/frame pointer */           \
    _(s1)                                              \
    _(a0) /* function arguments / return values */     \
    _(a1)                                              \
    _(a2) /* function arguments */                     \
    _(a3)                                              \
    _(a4)                                              \
    _(a5)                                              \
    IIF(RV32_HAS(RV32E))(,                             \
        _(a6)                                          \
        _(a7)                                          \
        _(s2) /* saved register */                     \
        _(s3)                                          \
        _(s4)                                          \
        _(s5)                                          \
        _(s6)                                          \
        _(s7)                                          \
        _(s8)                                          \
        _(s9)                                          \
        _(s10)                                         \
        _(s11)                                         \
        _(t3) /* temporary register */                 \
        _(t4)                                          \
        _(t5)                                          \
        _(t6)                                          \
    )
/* clang-format on */

/* RISC-V registers (mnemonics, ABI names)
 *
 * There are 32 registers in RISC-V. The program counter is a further register
 * "pc" that is present.
 *
 * There is no dedicated register that is used for the stack pointer, or
 * subroutine return address. The instruction encoding allows any x register
 * to be used for that purpose.
 *
 * However the standard calling conventions uses "x1" to store the return
 * address of call,  with "x5" as an alternative link register, and "x2" as
 * the stack pointer.
 */
/* clang-format off */
enum {
#define _(r) rv_reg_##r,
    RV_REGS_LIST
#undef _
    N_RV_REGS
};
/* clang-format on */

#define RV_PG_SHIFT 12
#define RV_PG_SIZE (1 << RV_PG_SHIFT)

typedef uint32_t pte_t;
#define PTE_V (1U)
#define PTE_R (1U << 1)
#define PTE_W (1U << 2)
#define PTE_X (1U << 3)
#define PTE_U (1U << 4)
#define PTE_G (1U << 5)
#define PTE_A (1U << 6)
#define PTE_D (1U << 7)

/* PTE XWRV bit in order */
enum SV32_PTE_PERM {
    NEXT_PG_TBL = 0b0001,
    RO_PAGE = 0b0011,
    RW_PAGE = 0b0111,
    EO_PAGE = 0b1001,
    RX_PAGE = 0b1011,
    RWX_PAGE = 0b1111,
    RESRV_PAGE1 = 0b0101,
    RESRV_PAGE2 = 0b1101,
};

#define MISA_SUPER (1 << ('S' - 'A'))
#define MISA_USER (1 << ('U' - 'A'))
#define MISA_I (1 << ('I' - 'A'))
#define MISA_E (1 << ('E' - 'A'))
#define MISA_M (1 << ('M' - 'A'))
#define MISA_A (1 << ('A' - 'A'))
#define MISA_F (1 << ('F' - 'A'))
#define MISA_C (1 << ('C' - 'A'))

/*
 * The mstatus register keeps track of and controls the hart's current
 * operating state. Using enum ensures true compile-time constants usable
 * in switch cases, array sizes, and static assertions.
 */
/* clang-format off */
enum {
    /* mstatus fields: shift values and masks */
    MSTATUS_SIE_SHIFT  = 1,  MSTATUS_SIE  = (1U << 1),
    MSTATUS_MIE_SHIFT  = 3,  MSTATUS_MIE  = (1U << 3),
    MSTATUS_SPIE_SHIFT = 5,  MSTATUS_SPIE = (1U << 5),
    MSTATUS_UBE_SHIFT  = 6,  MSTATUS_UBE  = (1U << 6),
    MSTATUS_MPIE_SHIFT = 7,  MSTATUS_MPIE = (1U << 7),
    MSTATUS_SPP_SHIFT  = 8,  MSTATUS_SPP  = (1U << 8),
    MSTATUS_MPP_SHIFT  = 11, MSTATUS_MPP  = (3U << 11),  /* 2-bit field */
    MSTATUS_MPRV_SHIFT = 17, MSTATUS_MPRV = (1U << 17),
    MSTATUS_SUM_SHIFT  = 18, MSTATUS_SUM  = (1U << 18),
    MSTATUS_MXR_SHIFT  = 19, MSTATUS_MXR  = (1U << 19),
    MSTATUS_TVM_SHIFT  = 20, MSTATUS_TVM  = (1U << 20),
    MSTATUS_TW_SHIFT   = 21, MSTATUS_TW   = (1U << 21),
    MSTATUS_TSR_SHIFT  = 22, MSTATUS_TSR  = (1U << 22),
};
/* clang-format on */

/* sstatus: restricted view of mstatus (same bit positions) */
#define SSTATUS_SIE_SHIFT MSTATUS_SIE_SHIFT
#define SSTATUS_SPIE_SHIFT MSTATUS_SPIE_SHIFT
#define SSTATUS_UBE_SHIFT MSTATUS_UBE_SHIFT
#define SSTATUS_SPP_SHIFT MSTATUS_SPP_SHIFT
#define SSTATUS_SUM_SHIFT MSTATUS_SUM_SHIFT
#define SSTATUS_MXR_SHIFT MSTATUS_MXR_SHIFT
#define SSTATUS_SIE MSTATUS_SIE
#define SSTATUS_SPIE MSTATUS_SPIE
#define SSTATUS_UBE MSTATUS_UBE
#define SSTATUS_SPP MSTATUS_SPP
#define SSTATUS_SUM MSTATUS_SUM
#define SSTATUS_MXR MSTATUS_MXR
#define SIP_SSIP_SHIFT 1
#define SIP_STIP_SHIFT 5
#define SIP_SEIP_SHIFT 9
#define SIP_SSIP (1 << SIP_SSIP_SHIFT)
#define SIP_STIP (1 << SIP_STIP_SHIFT)
#define SIP_SEIP (1 << SIP_SEIP_SHIFT)

#define RV_PRIV_U_MODE 0
#define RV_PRIV_S_MODE 1
#define RV_PRIV_M_MODE 3
#define RV_PRIV_IS_U_OR_S_MODE() (rv->priv_mode <= RV_PRIV_S_MODE)

#define RV_MVENDORID 0x12345678
#define RV_MARCHID ((1ULL << 31) | 1)
#define RV_MIMPID 1

#define RV_INT_STI_SHIFT 5
#define RV_INT_STI (1 << RV_INT_STI_SHIFT)

/* clang-format off */
enum TRAP_CODE {
#if !RV32_HAS(EXT_C)
    INSN_MISALIGNED = 0,                       /* Instruction address misaligned */
#endif /* !RV32_HAS(EXT_C) */
    ILLEGAL_INSN = 2,                          /* Illegal instruction */
    BREAKPOINT = 3,                            /* Breakpoint */
    LOAD_MISALIGNED = 4,                       /* Load address misaligned */
    STORE_MISALIGNED = 6,                      /* Store/AMO address misaligned */
#if RV32_HAS(SYSTEM)
    PAGEFAULT_INSN = 12,                       /* Instruction page fault */
    PAGEFAULT_LOAD = 13,                       /* Load page fault */
    PAGEFAULT_STORE = 15,                      /* Store page fault */
    SUPERVISOR_SW_INTR = (1U << 31) | 1,       /* Supervisor software interrupt */
    SUPERVISOR_TIMER_INTR = (1U << 31) | 5,    /* Supervisor timer interrupt */
    SUPERVISOR_EXTERNAL_INTR = (1U << 31) | 9, /* Supervisor external interrupt */
    ECALL_U = 8,                               /* Environment call from U-mode */
#endif /* RV32_HAS(SYSTEM) */
#if !RV32_HAS(SYSTEM)
    ECALL_M = 11,              /* Environment call from M-mode */
#endif /* !RV32_HAS(SYSTEM) */
};
/* clang-format on */

/*
 * For simplicity and clarity, abstracting m/scause and m/stval
 * into a cause and tval identifier respectively.
 */
/* clang-format off */
#define SET_CAUSE_AND_TVAL_THEN_TRAP(rv, cause, tval)                          \
    {                                                                          \
        /*                                                                     \
         * To align rv32emu behavior with Spike                                \
         *                                                                     \
         * If not in system mode, the __trap_handler                           \
         * should be be invoked                                                \
         *                                                                     \
         * FIXME: ECALL_U cannot be trap directly to __trap_handler            \
         */                                                                    \
        IIF(RV32_HAS(SYSTEM))(if (cause != ECALL_U) rv->is_trapped = true;, ); \
        if (RV_PRIV_IS_U_OR_S_MODE()) {                                        \
            rv->csr_scause = cause;                                            \
            rv->csr_stval = tval;                                              \
        } else {                                                               \
            rv->csr_mcause = cause;                                            \
            rv->csr_mtval = tval;                                              \
        }                                                                      \
        rv->io.on_trap(rv);                                                    \
    }
/* clang-format on */

/*
 * SBI functions must return a pair of values:
 *
 * struct sbiret {
 *     long error;
 *     long value;
 * };
 *
 * The error and value field will be set to register a0 and a1 respectively
 * after the SBI function return. The error field indicate whether the
 * SBI call is success or not. SBI_SUCCESS indicates success and
 * SBI_ERR_NOT_SUPPORTED indicates not supported failure. The value field is
 * the information based on the extension ID(EID) and SBI function ID(FID).
 *
 * SBI reference: https://github.com/riscv-non-isa/riscv-sbi-doc
 */
#define SBI_SUCCESS 0
#define SBI_ERR_NOT_SUPPORTED -2

/*
 * All of the functions in the base extension must be supported by
 * all SBI implementations.
 */
#define SBI_EID_BASE 0x10
#define SBI_BASE_GET_SBI_SPEC_VERSION 0
#define SBI_BASE_GET_SBI_IMPL_ID 1
#define SBI_BASE_GET_SBI_IMPL_VERSION 2
#define SBI_BASE_PROBE_EXTENSION 3
#define SBI_BASE_GET_MVENDORID 4
#define SBI_BASE_GET_MARCHID 5
#define SBI_BASE_GET_MIMPID 6

/* Make supervisor to schedule the clock for next timer event. */
#define SBI_EID_TIMER 0x54494D45
#define SBI_TIMER_SET_TIMER 0

/* Allows the supervisor to request system-level reboot or shutdown. */
#define SBI_EID_RST 0x53525354
#define SBI_RST_SYSTEM_RESET 0
#define SBI_RST_TYPE_SHUTDOWN 0x0
#define SBI_RST_TYPE_COLD_REBOOT 0x1
#define SBI_RST_TYPE_WARM_REBOOT 0x2
#define NO_REASON 0x0
#define SYSTEM_FAILURE 0x1

#define BLOCK_MAP_CAPACITY_BITS 10

/* forward declaration for internal structure */
typedef struct riscv_internal riscv_t;
typedef void *riscv_user_t;

typedef uint32_t riscv_word_t;
typedef uint16_t riscv_half_t;
typedef uint8_t riscv_byte_t;
typedef uint32_t riscv_exception_t;
#if RV32_HAS(EXT_F)
typedef softfloat_float32_t riscv_float_t;
#endif

/* memory read handlers */
typedef riscv_word_t (*riscv_mem_ifetch)(riscv_t *rv, riscv_word_t addr);
typedef riscv_word_t (*riscv_mem_read_w)(riscv_t *rv, riscv_word_t addr);
typedef riscv_half_t (*riscv_mem_read_s)(riscv_t *rv, riscv_word_t addr);
typedef riscv_byte_t (*riscv_mem_read_b)(riscv_t *rv, riscv_word_t addr);

/* memory write handlers */
typedef void (*riscv_mem_write_w)(riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_word_t data);
typedef void (*riscv_mem_write_s)(riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_half_t data);
typedef void (*riscv_mem_write_b)(riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_byte_t data);
#if RV32_HAS(SYSTEM)
/*
 * VA2PA handler
 * The MMU walkers and fault checkers are defined in system.c
 * Thus, exporting this handler through function pointer
 * preserves the encapsulation of MMU translation.
 *
 * ifetch do not leverage this translation because basic block
 * might be retranslated and the corresponding PTE is NULL.
 */
typedef riscv_word_t (*riscv_mem_translate_t)(riscv_t *rv,
                                              riscv_word_t vaddr,
                                              bool rw);

/* MMU memory access function pointers for T2C runtime binding.
 * These avoid embedding compile-time function addresses in JIT code,
 * which would break with ASLR on x86-64 Linux.
 */
typedef riscv_word_t (*riscv_mmu_read_w_t)(riscv_t *rv, riscv_word_t vaddr);
typedef riscv_half_t (*riscv_mmu_read_s_t)(riscv_t *rv, riscv_word_t vaddr);
typedef riscv_byte_t (*riscv_mmu_read_b_t)(riscv_t *rv, riscv_word_t vaddr);
typedef void (*riscv_mmu_write_w_t)(riscv_t *rv,
                                    riscv_word_t vaddr,
                                    riscv_word_t val);
typedef void (*riscv_mmu_write_s_t)(riscv_t *rv,
                                    riscv_word_t vaddr,
                                    riscv_half_t val);
typedef void (*riscv_mmu_write_b_t)(riscv_t *rv,
                                    riscv_word_t vaddr,
                                    riscv_byte_t val);
#endif

/* system instruction handlers */
typedef void (*riscv_on_ecall)(riscv_t *rv);
typedef void (*riscv_on_ebreak)(riscv_t *rv);
typedef void (*riscv_on_memset)(riscv_t *rv);
typedef void (*riscv_on_memcpy)(riscv_t *rv);
typedef void (*riscv_on_trap)(riscv_t *rv);
/* RISC-V emulator I/O interface */
typedef struct {
    /* memory read interface */
    riscv_mem_ifetch mem_ifetch;
    riscv_mem_read_w mem_read_w;
    riscv_mem_read_s mem_read_s;
    riscv_mem_read_b mem_read_b;

    /* memory write interface */
    riscv_mem_write_w mem_write_w;
    riscv_mem_write_s mem_write_s;
    riscv_mem_write_b mem_write_b;

#if RV32_HAS(SYSTEM)
    riscv_mem_translate_t mem_translate;

    /* MMU memory access functions for T2C runtime binding */
    riscv_mmu_read_w_t mmu_read_w;
    riscv_mmu_read_s_t mmu_read_s;
    riscv_mmu_read_b_t mmu_read_b;
    riscv_mmu_write_w_t mmu_write_w;
    riscv_mmu_write_s_t mmu_write_s;
    riscv_mmu_write_b_t mmu_write_b;
#endif

    /* system */
    riscv_on_ecall on_ecall;
    riscv_on_ebreak on_ebreak;
    riscv_on_memset on_memset;
    riscv_on_memcpy on_memcpy;
    riscv_on_trap on_trap;
} riscv_io_t;

/* run emulation */
void rv_run(riscv_t *rv);

/* create a RISC-V emulator */
riscv_t *rv_create(riscv_user_t attr);

/* delete a RISC-V emulator */
void rv_delete(riscv_t *rv);

#if RV32_HAS(T2C)
/* cleanup t2c thread */
void rv_terminate_t2c(riscv_t *rv);
#endif

/* Cold reboot the system
 *
 * The first power on is considered as a cold reboot
 *
 * Reset the RISC-V processor, memory and peripheral
 */
bool rv_cold_reboot(riscv_t *rv, riscv_word_t pc);

/* Only system emulation needs warm reboot */
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
/* Warm reboot the system
 *
 * Only reset the RISC-V processor and memory
 */
void rv_warm_reboot(riscv_t *rv, riscv_word_t pc);
#endif

#if RV32_HAS(GDBSTUB)
/* Run the RISC-V emulator as gdbstub */
void rv_debug(riscv_t *rv);
#endif

/* step the RISC-V emulator */
void rv_step(void *arg);

/* step the RISC-V emulator for debug mode */
void rv_step_debug(void *arg);

/* set the program counter of a RISC-V emulator */
bool rv_set_pc(riscv_t *rv, riscv_word_t pc);

/* get the program counter of a RISC-V emulator */
riscv_word_t rv_get_pc(riscv_t *rv);

/* set a register of the RISC-V emulator */
void rv_set_reg(riscv_t *rv, uint32_t reg, riscv_word_t in);

typedef struct {
    int fd;
    FILE *file;
} fd_stream_pair_t;

/* remap standard stream to other stream */
void rv_remap_stdstream(riscv_t *rv, fd_stream_pair_t *fsp, uint32_t fsp_size);

/* get a register of the RISC-V emulator */
riscv_word_t rv_get_reg(riscv_t *rv, uint32_t reg);

/* system call handler */
void syscall_handler(riscv_t *rv);

/* environment call handler */
void ecall_handler(riscv_t *rv);

/* trap handler */
void trap_handler(riscv_t *rv);

/* memset handler */
void memset_handler(riscv_t *rv);

/* memcpy handler */
void memcpy_handler(riscv_t *rv);

/* dump registers as JSON to out_file_path */
void dump_registers(riscv_t *rv, char *out_file_path);

/* breakpoint exception handler */
void ebreak_handler(riscv_t *rv);

/*
 * Trap might occurs during block emulation. For instance, page fault.
 * In order to handle trap, we have to escape from block and execute
 * registered trap handler. This trap_handler function helps to execute
 * the registered trap handler, PC by PC. Once the trap is handled,
 * resume the previous execution flow where cause the trap.
 *
 * Now, rv32emu supports misaligned access and page fault handling.
 */
void trap_handler(riscv_t *rv);

/* halt the core */
void rv_halt(riscv_t *rv);

/* return the halt state */
bool rv_has_halted(riscv_t *rv);

#if RV32_HAS(ARCH_TEST)
/* Set tohost/fromhost addresses for architectural testing */
void rv_set_tohost_addr(riscv_t *rv, uint32_t addr);
void rv_set_fromhost_addr(riscv_t *rv, uint32_t addr);
#endif

#if RV32_HAS(SYSTEM)
/* TLB management functions for SFENCE.VMA instruction and SATP changes.
 * Invalidate cached address translations when page tables are modified.
 */
void mmu_tlb_flush_all(riscv_t *rv);
void mmu_tlb_flush(riscv_t *rv, uint32_t vaddr);
#endif

enum {
    /* run and trace instructions and print them out during emulation */
    RV_RUN_TRACE = 1,

    /* run as gdbstub during emulation */
    RV_RUN_GDBSTUB = 2,

    /* run and profile relationship of blocks and save to prof_output_file
       during emulation */
    RV_RUN_PROFILE = 4,
};

typedef struct {
    char *elf_program;
} vm_user_t;

#if RV32_HAS(SYSTEM)
typedef struct {
    char *kernel;
    char *initrd;
    char *bootargs;
    char **vblk_device;
    int vblk_device_cnt;
} vm_system_t;
#endif /* RV32_HAS(SYSTEM) */

typedef struct {
#if !RV32_HAS(SYSTEM_MMIO)
    vm_user_t user;
#else
    vm_system_t system;
#endif /* !RV32_HAS(SYSTEM_MMIO) */

} vm_data_t;

typedef struct {
#if RV32_HAS(SYSTEM_MMIO)
    /* uart object */
    u8250_state_t *uart;

    /* plic object */
    plic_t *plic;

#if RV32_HAS(GOLDFISH_RTC)
    /* rtc object */
    rtc_t *rtc;
#endif /* RV32_HAS(GOLDFISH_RTC) */

    /* virtio-blk device */
    uint32_t **disk;
    virtio_blk_state_t **vblk;
    virtio_blk_state_t *vblk_curr;
    uint32_t vblk_mmio_base_hi;
    uint32_t vblk_mmio_max_hi;
    int vblk_irq_base;
    int vblk_cnt;
#endif /* RV32_HAS(SYSTEM_MMIO) */

    /* vm memory object */
    memory_t *mem;

    /* max memory size is 2^32 bytes (4GB).
     * Use uint64_t to support full 4GB address space with demand paging.
     * Physical memory is allocated on-demand, keeping actual usage minimal.
     */
    uint64_t mem_size;

    /* vm main stack size */
    uint32_t stack_size;

    /* To deal with the RV32 ABI for accessing args list,
     * offset of args data have to be saved.
     *
     * args_offset_size is the memory size to store the offset
     */
    uint32_t args_offset_size;

    /* arguments of emulation program */
    int argc;
    char **argv;
    /* FIXME: cannot access envp yet */

    /* emulation program exit code */
    int exit_code;

    /* emulation program error code */
    int error;

    /* log level */
    int log_level;

    /* userspace or system emulation data */
    vm_data_t data;

    /* number of cycle(instruction) in a rv_step call*/
    int cycle_per_step;

    /* allow misaligned memory access */
    bool allow_misalign;

    /* run flag, it is the bitwise OR from
     * RV_RUN_TRACE, RV_RUN_GDBSTUB, and RV_RUN_PROFILE
     */
    uint8_t run_flag;

    /* profiling output file if RV_RUN_PROFILE is set in run_flag */
    char *profile_output_file;

    /* set by rv_create during initialization.
     * use rv_remap_stdstream to overwrite them
     */
    int fd_stdin, fd_stdout, fd_stderr;

    /* vm file descriptor map: int -> (FILE *) */
    map_t fd_map;

    /* the data segment break address */
    riscv_word_t break_addr;

#if !RV32_HAS(SYSTEM)
    /* the exit entry address */
    riscv_word_t exit_addr;

    /* flag to determine if the emulator exits the target program */
    bool on_exit;
#endif

#if RV32_HAS(SDL) && RV32_HAS(SYSTEM_MMIO)
    /* flag to determine if running SDL program in guestOS */
    bool running_sdl;
#endif /* SDL */

    /* SBI timer */
    uint64_t timer;

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    /* DTB address in memory */
    uint32_t dtb_addr;
#endif
} vm_attr_t;

#ifdef __cplusplus
};
#endif
