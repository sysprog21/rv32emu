/* <stdbool.h> */
typedef enum { false, true } bool;

/* <stdint.h> */
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef char int8_t;
typedef unsigned char uint8_t;

#define RV_NUM_REGS 32

enum {
    INSN_16 = 2,
    INSN_32 = 4,
};

enum {
    rv_reg_zero = 0,
};

typedef void *riscv_user_t;
typedef uint32_t riscv_word_t;
typedef uint16_t riscv_half_t;
typedef uint8_t riscv_byte_t;
typedef uint32_t riscv_exception_t;
typedef float riscv_float_t;
typedef riscv_word_t (*riscv_mem_ifetch)(struct riscv_t *rv, riscv_word_t addr);
typedef riscv_word_t (*riscv_mem_read_w)(struct riscv_t *rv, riscv_word_t addr);
typedef riscv_half_t (*riscv_mem_read_s)(struct riscv_t *rv, riscv_word_t addr);
typedef riscv_byte_t (*riscv_mem_read_b)(struct riscv_t *rv, riscv_word_t addr);
typedef void (*riscv_mem_write_w)(struct riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_word_t data);
typedef void (*riscv_mem_write_s)(struct riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_half_t data);
typedef void (*riscv_mem_write_b)(struct riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_byte_t data);
typedef void (*riscv_on_ecall)(struct riscv_t *rv);
typedef void (*riscv_on_ebreak)(struct riscv_t *rv);
struct riscv_io_t {
    riscv_mem_ifetch mem_ifetch;
    riscv_mem_read_w mem_read_w;
    riscv_mem_read_s mem_read_s;
    riscv_mem_read_b mem_read_b;

    riscv_mem_write_w mem_write_w;
    riscv_mem_write_s mem_write_s;
    riscv_mem_write_b mem_write_b;

    riscv_on_ecall on_ecall;
    riscv_on_ebreak on_ebreak;
};

struct riscv_t {
    bool halt;
    struct riscv_io_t io;
    riscv_word_t X[RV_NUM_REGS];
    riscv_word_t PC;
    riscv_user_t userdata;
    uint64_t csr_cycle;
    uint32_t csr_mstatus;
    uint32_t csr_mtvec;
    uint32_t csr_misa;
    uint32_t csr_mtval;
    uint32_t csr_mcause;
    uint32_t csr_mscratch;
    uint32_t csr_mepc;
    uint32_t csr_mip;
    uint32_t csr_mbadaddr;
    uint8_t insn_len;
};
