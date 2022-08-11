#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jit.h"

#if !defined(__LP64__) && !defined(_LP64)
#error "JIT templete assumes LP64 data model."
#endif
#include "jit_template.h"

#include "mir-gen.h"

/* hash function is used when mapping addresses to indexes in the block map */
static uint32_t hash(size_t k)
{
    k ^= k << 21;
    k ^= k >> 17;
#if (SIZE_MAX > 0xFFFFFFFF)
    k ^= k >> 35;
    k ^= k >> 51;
#endif
    return k;
}

static void str2buffer(rv_buffer *buffer, const char *fmt, ...)
{
    va_list args;
    char code[1024];

    va_start(args, fmt);
    int n = vsnprintf(code, sizeof(code), fmt, args);
    va_end(args);

    if (n < 0)
        abort();

    /* x0 is always zero */
    /* FIXME: make it less ugly */
    char *p = strstr(code, "(int32_t) rv->X[0]");
    if (p)
        memcpy(p, "0                 ", 18);
    p = strstr(code, "rv->X[0]");
    if (p)
        memcpy(p, "0       ", 8);

    size_t len = strlen(code);
    size_t new_size = buffer->size + len;
    if (new_size > buffer->capacity) {
        buffer->src = realloc(buffer->src, new_size);
        buffer->capacity = new_size;
    }
    strncpy(&buffer->src[buffer->size], code, len);
    buffer->size = new_size;
}

#define CODE(fmt, ...) str2buffer(buff, fmt, ##__VA_ARGS__)
#define DECLARE_FUNC(name) CODE("void %s(struct riscv_t *rv) {\n", name)

#define COMMENT(insn) CODE("\n// %s\n", insn);
#define DECLARE_VAR                                               \
    CODE(                                                         \
        "uint32_t addr, data;\n"                                  \
        "uint32_t a_u32, b_u32, tmp_u32, res_u32, dividend_u32, " \
        "divisor_u32, pc, ra;\n"                                  \
        "int32_t a, b, res, dividend, divisor;\n"                 \
        "int64_t a64, b64;\n"                                     \
        "bool taken;\n"                                           \
        "uint64_t b_u64;\n")
#define END CODE("\n}\n")
#define END_FUNC CODE("\n}\n")
#define UPDATE_PC(val) CODE("rv->PC += %u;\n", val)
#define UPDATE_INSN_LEN(len) CODE("rv->insn_len = %u;\n", len)
#define UPDATE_INSN16_LEN CODE("rv->insn_len = %u;\n", INSN_16)
#define UPDATE_INSN32_LEN CODE("rv->insn_len = %u;\n", INSN_32)
#define LOAD_ADDR CODE("addr = rv->X[%u] + %d;", rs1, imm)

#define insn_misaligned CODE("rv_except_insn_misaligned(rv, %u);\n", pc);

#define load_misaligned(num)        \
    CODE("if(addr & %d) {\n", num); \
    CODE("rv_except_load_misaligned(rv, addr);}\n");

#define store_misaligned(num)       \
    CODE("if(addr & %d) {\n", num); \
    CODE("rv_except_store_misaligned(rv, addr);}\n");

#define illegal_insn CODE("rv_except_illegal_insn(rv, %u);}\n", insn)

enum {
    op_load = 0b00000,
    op_load_fp = 0b00001,
    op_misc_mem = 0b00011,
    op_op_imm = 0b00100,
    op_auipc = 0b00101,

    op_store = 0b01000,
    op_store_fp = 0b01001,
    op_amo = 0b01011,
    op_op = 0b01100,
    op_lui = 0b01101,

    op_madd = 0b10000,
    op_msub = 0b10001,
    op_nmsub = 0b10010,
    op_nmadd = 0b10011,
    op_fp = 0b10100,

    op_branch = 0b11000,
    op_jalr = 0b11001,
    op_jal = 0b11011,
    _op_system = 0b11100,
};

static void emit_load(rv_buffer *buff, uint32_t insn, struct riscv_t *rv UNUSED)
{
    const int32_t imm = dec_itype_imm(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t funct3 = dec_funct3(insn);
    const uint32_t rd = dec_rd(insn);

    /* load address */
    LOAD_ADDR;
    bool end = true;
    switch (funct3) {
    case 0: /* LB */
        COMMENT("LB");
        CODE("rv->X[%u] = sign_extend_b(rv->io.mem_read_b(rv, addr));\n", rd);
        goto update;
    case 1: /* LH */
        COMMENT("LH");
        load_misaligned(1);
        CODE(
            "else {\nrv->X[%u] = sign_extend_h(rv->io.mem_read_s(rv, addr));\n",
            rd);
        end = false;
        goto update;
    case 2: /* LW */
        COMMENT("LW");
        load_misaligned(3);
        CODE("else {\nrv->X[%u] = rv->io.mem_read_w(rv, addr);\n", rd);
        end = false;
        goto update;
    case 4: /* LBU */
        COMMENT("LBU");
        CODE("rv->X[%u] = rv->io.mem_read_b(rv, addr);\n", rd);
        goto update;
    case 5: /* LHU */
        COMMENT("LHU");
        load_misaligned(1);
        CODE("else {\nrv->X[%u] = rv->io.mem_read_s(rv, addr);\n", rd);
        end = false;
        goto update;
    default:
        illegal_insn;
        return;
    }
update:
    CODE("rv->PC += rv->insn_len;\n");
    UPDATE_INSN32_LEN;

    if (!end)
        END;
}

static void emit_op_imm(rv_buffer *buff,
                        uint32_t insn,
                        struct riscv_t *rv UNUSED)
{
    /* I-type decoding */
    const int32_t imm = dec_itype_imm(insn);
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t funct3 = dec_funct3(insn);

    /* dispatch operation type */
    switch (funct3) {
    case 0: /* ADDI */
        COMMENT("ADDI");
        if (rs1 == rv_reg_zero)
            CODE("rv->X[%u] = %d;\n", rd, imm);
        else
            CODE("rv->X[%u] = (int32_t) (rv->X[%u]) + %d;\n", rd, rs1, imm);
        goto update;
    case 1: /* SLLI */
        COMMENT("SLLI");
        CODE("rv->X[%u] = rv->X[%u] << (%d & 0x1f);\n", rd, rs1, imm);
        goto update;
    case 2: /* SLTI */
        COMMENT("SLTI");
        CODE("rv->X[%u] = ((int32_t) (rv->X[%u]) < %d) ? 1 : 0;\n", rd, rs1,
             imm);
        goto update;
    case 3: /* SLTIU */
        COMMENT("SLTIU");
        CODE("rv->X[%u] = (rv->X[%u] < (uint32_t) %d) ? 1 : 0;\n", rd, rs1,
             imm);
        goto update;
    case 4: /* XORI */
        COMMENT("XORI");
        CODE("rv->X[%u] = rv->X[%u] ^ %d;\n", rd, rs1, imm);
        goto update;
    case 5:
        if (imm & ~0x1f) { /* SRAI */
            COMMENT("SRAI");
            CODE("rv->X[%u] = ((int32_t) rv->X[%u]) >> (%d & 0x1f);\n", rd, rs1,
                 imm);
        } else { /* SRLI */
            COMMENT("SRLI");
            CODE("rv->X[%u] = rv->X[%u] >> (%d & 0x1f);\n", rd, rs1, imm);
        }
        goto update;
    case 6: /* ORI */
        COMMENT("ORI");
        CODE("rv->X[%u] = rv->X[%u] | %d;\n", rd, rs1, imm);
        goto update;
    case 7: /* ANDI */
        COMMENT("ANDI");
        CODE("rv->X[%u] = rv->X[%u] & %d;\n", rd, rs1, imm);
        goto update;
    default:
        illegal_insn;
        return;
    }
update:
    CODE("rv->PC += rv->insn_len;\n");
    UPDATE_INSN32_LEN;
}

static void emit_auipc(rv_buffer *buff,
                       uint32_t insn,
                       struct riscv_t *rv UNUSED)
{
    /* U-type decoding */
    const uint32_t rd = dec_rd(insn);
    const uint32_t imm = dec_utype_imm(insn);
    COMMENT("AUIPC");
    CODE("rv->X[%u] = %u + rv->PC;\n", rd, imm);
    CODE("rv->PC += rv->insn_len;\n");
    UPDATE_INSN32_LEN;
}

static void emit_store(rv_buffer *buff,
                       uint32_t insn,
                       struct riscv_t *rv UNUSED)
{
    /* S-type format */
    const int32_t imm = dec_stype_imm(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);
    const uint32_t funct3 = dec_funct3(insn);

    /* store address */
    LOAD_ADDR;
    CODE("data = rv->X[%u];\n", rs2);
    bool end = true;

    /* dispatch by write size */
    switch (funct3) {
    case 0: /* SB */
        COMMENT("SB");
        CODE("rv->io.mem_write_b(rv, addr, data);\n");
        goto update;
    case 1: /* SH */
        COMMENT("SH");
        store_misaligned(1);
        CODE(" else {\nrv->io.mem_write_s(rv, addr, data);\n");
        end = false;
        goto update;
    case 2: /* SW */
        COMMENT("SW");
        store_misaligned(3);
        CODE(" else {\nrv->io.mem_write_w(rv, addr, data);\n");
        end = false;
        goto update;
    default:
        illegal_insn;
        return;
    }
update:
    UPDATE_PC(4);
    UPDATE_INSN32_LEN;

    if (!end)
        END;
}

static void emit_amo(rv_buffer *buff, uint32_t insn, struct riscv_t *rv UNUSED)
{
#ifdef ENABLE_RV32A
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);
    const uint32_t f7 = dec_funct7(insn);
    const uint32_t funct5 = (f7 >> 2) & 0x1f;

    switch (funct5) {
    case 0b00010: /* LR.W */
        COMMENT("LR.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, rv->X[%u]);\n", rd, rs1);
        goto update;
    case 0b00011: /* SC.W */
        COMMENT("SC.W");
        CODE("rv->io.mem_write_w(rv, rv->X[%u], rv->X[%u]);\n", rs1, rs2);
        CODE("rv->X[%u] = 0;\n", rd);
        goto update;
    case 0b00001: { /* AMOSWAP.W */
        COMMENT("AMOSWAP.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("rv->io.mem_write_s(rv, %u, rv->X[%u]);\n", rs1, rs2);
        goto update;
    }
    case 0b00000: { /* AMOADD.W */
        COMMENT("AMOADD.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("res = (int32_t) rv->X[%u] + (int32_t) rv->X[%u];\n", rd, rs2);
        CODE("rv->io.mem_write_s(rv, %u, res);\n", rs1);
        goto update;
    }
    case 0b00100: { /* AMOXOR.W */
        COMMENT("AMOXOR.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("res = rv->X[%u] ^ rv->X[%u];\n", rd, rs2);
        CODE("rv->io.mem_write_s(rv, %u, res);\n", rs1);
        goto update;
    }
    case 0b01100: { /* AMOAND.W */
        COMMENT("AMOAND.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("res = rv->X[%u] & rv->X[%u];\n", rd, rs2);
        CODE("rv->io.mem_write_s(rv, %u, res);\n", rs1);
        goto update;
    }
    case 0b01000: { /* AMOOR.W */
        COMMENT("AMOOR.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("res = rv->X[%u] | rv->X[%u];\n", rd, rs2);
        CODE("rv->io.mem_write_s(rv, %u, res);\n", rs1);
        goto update;
    }
    case 0b10000: { /* AMOMIN.W */
        COMMENT("AMOMIN.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("a = rv->X[%u];\n", rd);
        CODE("b = rv->X[%u];\n", rs2);
        CODE("res = a < b ? a : b;\n");
        CODE("rv->io.mem_write_s(rv, %u, res);\n", rs1);
        goto update;
    }
    case 0b10100: { /* AMOMAX.W */
        COMMENT("AMOMAX.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("a = rv->X[%u];\n", rd);
        CODE("b = rv->X[%u];\n", rs2);
        CODE("res = a > b ? a : b;\n");
        CODE("rv->io.mem_write_s(rv, %u, res);\n", rs1);
        goto update;
    }
    case 0b11000: { /* AMOMINU.W */
        COMMENT("AMOMINU.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("a_u32 = rv->X[%u];\n", rd);
        CODE("b_u32 = rv->X[%u];\n", rs2);
        CODE("res_u32 = a_u32 < b_u32 ? a_u32 : b_u32;\n");
        CODE("rv->io.mem_write_s(rv, %u, res_u32);\n", rs1);
        goto update;
    }
    case 0b11100: { /* AMOMAXU.W */
        COMMENT("AMOMAXU.W");
        CODE("rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", rd, rs1);
        CODE("a_u32 = rv->X[%u];\n", rd);
        CODE("b_u32 = rv->X[%u];\n", rs2);
        CODE("res_u32 = a_u32 > b_u32 ? a_u32 : b_u32;\n");
        CODE("rv->io.mem_write_s(rv, %u, res_u32);\n", rs1);
        goto update;
    }
    default:
        illegal_insn;
        return;
    }
update:
    UPDATE_PC(4);
    UPDATE_INSN32_LEN;
#endif
}

static void emit_op(rv_buffer *buff, uint32_t insn, struct riscv_t *rv UNUSED)
{
    const uint32_t rd = dec_rd(insn);
    const uint32_t funct3 = dec_funct3(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);
    const uint32_t funct7 = dec_funct7(insn);

    switch (funct7) {
    case 0b0000000:
        switch (funct3) {
        case 0b000: /* ADD */
            COMMENT("ADD");
            if (rs1 == rv_reg_zero || rs2 == rv_reg_zero)
                CODE("rv->X[%u] = (int32_t) (rv->X[%u]);\n", rd, rs1 | rs2);
            else
                CODE(
                    "rv->X[%u] = (int32_t) (rv->X[%u]) + (int32_t) "
                    "(rv->X[%u]);",
                    rd, rs1, rs2);
            goto update;
        case 0b001: /* SLL */
            COMMENT("SLL");
            CODE("rv->X[%u] = rv->X[%u] << (rv->X[%u] & 0x1f);", rd, rs1, rs2);
            goto update;
        case 0b010: /* SLT */
            COMMENT("SLT");
            CODE(
                "rv->X[%u] = ((int32_t) (rv->X[%u]) < (int32_t) "
                "(rv->X[%u])) ? "
                "1 : 0;",
                rd, rs1, rs2);
            goto update;
        case 0b011: /* SLTU */
            COMMENT("SLTU");
            CODE("rv->X[%u] = (rv->X[%u] < rv->X[%u]) ? 1 : 0;", rd, rs1, rs2);
            goto update;
        case 0b100: /* XOR */
            COMMENT("XOR");
            CODE("rv->X[%u] = rv->X[%u] ^ rv->X[%u];", rd, rs1, rs2);
            goto update;
        case 0b101: /* SRL */
            COMMENT("SRL");
            CODE("rv->X[%u] = rv->X[%u] >> (rv->X[%u] & 0x1f);", rd, rs1, rs2);
            goto update;
        case 0b110: /* OR */
            COMMENT("OR");
            CODE("rv->X[%u] = rv->X[%u] | rv->X[%u];", rd, rs1, rs2);
            goto update;
        case 0b111: /* AND */
            COMMENT("AND");
            CODE("rv->X[%u] = rv->X[%u] & rv->X[%u];", rd, rs1, rs2);
            goto update;
        default:
            illegal_insn;
            return;
        }
        break;
#ifdef ENABLE_RV32M
    case 0b0000001: /* RV32M instructions */
        switch (funct3) {
        case 0b000: /* MUL */
            COMMENT("MUL");
            CODE("rv->X[%u] = (int32_t) rv->X[%u] * (int32_t) rv->X[%u];", rd,
                 rs1, rs2);
            goto update;
        case 0b001: /* MULH */
            COMMENT("MULH");
            CODE("a64 = (int32_t) rv->X[%u];", rs1);
            CODE("b64 = (int32_t) rv->X[%u];", rs2);
            CODE("rv->X[%u] = ((uint64_t) (a64 * b64)) >> 32;", rd);
            goto update;
        case 0b010: /* MULHSU */
            COMMENT("MULHSU");
            CODE("a64 = (int32_t) rv->X[%u];", rs1);
            CODE("b_u64 = rv->X[%u];", rs2);
            CODE("rv->X[%u] = ((uint64_t) (a64 * b_u64)) >> 32;", rd);
            goto update;
        case 0b011: /* MULHU */
            COMMENT("MULHU");
            CODE(
                "rv->X[%u] = ((uint64_t) rv->X[%u] * (uint64_t) rv->X[%u]) "
                ">> 32;",
                rd, rs1, rs2);
            goto update;
        case 0b100: /* DIV */
            COMMENT("DIV");
            CODE("dividend = (int32_t) rv->X[%u];", rs1);
            CODE("divisor = (int32_t) rv->X[%u];", rs2);
            CODE(
                "if (divisor == 0) {\n"
                "rv->X[%u] = ~0u;\n"
                "} else if (divisor == -1 && rv->X[%u] == 0x80000000u) {\n"
                "rv->X[%u] = rv->X[%u];\n"
                "} else {\n"
                "rv->X[%u] = dividend / divisor;\n"
                "}\n",
                rd, rs1, rd, rs1, rd);
            goto update;
        case 0b101: /* DIVU */
            COMMENT("DIVU");
            CODE("dividend_u32 = rv->X[%u];", rs1);
            CODE("divisor_u32 = rv->X[%u];", rs2);
            CODE(
                "if (divisor_u32 == 0) {\n"
                "rv->X[%u] = ~0u;\n"
                "} else {\n"
                "rv->X[%u] = dividend_u32 / divisor_u32;\n"
                "}\n",
                rd, rd);
            goto update;
        case 0b110: /* REM */
            COMMENT("REM");
            CODE("dividend = rv->X[%u];", rs1);
            CODE("divisor = rv->X[%u];", rs2);
            CODE(
                "if (divisor == 0) {"
                "rv->X[%u] = dividend;"
                "} else if (divisor == -1 && rv->X[%u] == 0x80000000u) {"
                "rv->X[%u] = 0;"
                "} else {"
                "rv->X[%u] = dividend %% divisor;"
                "}",
                rd, rs1, rd, rd);
            goto update;
        case 0b111: /* REMU */
            COMMENT("REMU");
            CODE("dividend_u32 = rv->X[%u];", rs1);
            CODE("divisor_u32 = rv->X[%u];", rs2);
            CODE(
                "if (divisor_u32 == 0) {"
                "    rv->X[%u] = dividend_u32;"
                "} else {"
                "    rv->X[%u] = dividend_u32 %% divisor_u32;"
                "}",
                rd, rd);
            goto update;
        default:
            illegal_insn;
            return;
        }
        break;
#endif /* ENABLE_RV32M */
    case 0b0100000:
        switch (funct3) {
        case 0b000: /* SUB */
            COMMENT("SUB");
            if (rs1 == rv_reg_zero)
                CODE("rv->X[%u] = - (int32_t) (rv->X[%u]);\n", rd, rs2);
            else
                CODE(
                    "rv->X[%u] = (int32_t) (rv->X[%u]) - "
                    "(int32_t) (rv->X[%u]);",
                    rd, rs1, rs2);
            goto update;
        case 0b101: /* SRA */
            COMMENT("SRA");
            CODE("rv->X[%u] = ((int32_t) rv->X[%u]) >> (rv->X[%u] & 0x1f);", rd,
                 rs1, rs2);
            goto update;
        default:
            illegal_insn;
            return;
        }
        break;
    default:
        illegal_insn;
        return;
    }
update:
    CODE("rv->PC += rv->insn_len;\n");
    UPDATE_INSN32_LEN;
}

static void emit_lui(rv_buffer *buff, uint32_t insn, struct riscv_t *rv UNUSED)
{
    COMMENT("LUI");
    /* U-type decoding */
    const uint32_t rd = dec_rd(insn);
    const uint32_t val = dec_utype_imm(insn);
    CODE("rv->X[%u] = %u;\n", rd, val);
    CODE("rv->PC += rv->insn_len;\n");
    UPDATE_INSN32_LEN;
}

static void emit_branch(rv_buffer *buff,
                        uint32_t insn,
                        struct riscv_t *rv UNUSED)
{
    /* B-type decoding */
    const uint32_t func3 = dec_funct3(insn);
    const int32_t imm = dec_btype_imm(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);

    /* track if branch is taken or not */
    CODE("taken = false;\n");
    CODE("pc = rv->PC;\n");

    /* dispatch by branch type */
    switch (func3) {
    case 0: /* BEQ */
        COMMENT("BEQ");
        CODE("taken = (rv->X[%u] == rv->X[%u]);\n", rs1, rs2);
        break;
    case 1: /* BNE */
        COMMENT("BNE");
        CODE("taken = (rv->X[%u] != rv->X[%u]);\n", rs1, rs2);
        break;
    case 4: /* BLT */
        COMMENT("BLT");
        CODE("taken = ((int32_t) rv->X[%u] < (int32_t) rv->X[%u]);\n", rs1,
             rs2);
        break;
    case 5: /* BGE */
        COMMENT("BGE");
        CODE("taken = ((int32_t) rv->X[%u] >= (int32_t) rv->X[%u]);\n", rs1,
             rs2);
        break;
    case 6: /* BLTU */
        COMMENT("BLTU");
        CODE("taken = (rv->X[%u] < rv->X[%u]);\n", rs1, rs2);
        break;
    case 7: /* BGEU */
        COMMENT("BGEU");
        CODE("taken = (rv->X[%u] >= rv->X[%u]);\n", rs1, rs2);
        break;
    default:
        CODE("rv_except_illegal_insn(rv, %u);\n", insn);
    }

    /* perform branch action */
    CODE(
        "if (taken) {\n"
        "rv->PC += %u;\n"
#ifdef ENABLE_RV32C
        "if (rv->PC & 0x1)\n"
#else
        "if (rv->PC & 0x3)\n"
#endif
        "rv_except_insn_misaligned(rv, pc);\n"
        "} else {\n"
        "rv->PC += rv->insn_len;\n"
        "}\n",
        imm);
}

static void emit_jalr(rv_buffer *buff, uint32_t insn, struct riscv_t *rv UNUSED)
{
    /* I-type decoding */
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const int32_t imm = dec_itype_imm(insn);
    COMMENT("JALR");

    CODE(
        "ra = rv->PC + rv->insn_len;\n"
        "pc = rv->PC;\n");
    /* jump */
    CODE("rv->PC = (rv->X[%u] + %d) & ~1u;\n", rs1, imm);
    /* link */
    if (rd != rv_reg_zero)
        CODE("rv->X[%u] = ra;\n", rd);

    /* check for exception */
    CODE(
#ifdef ENABLE_RV32C
        "if (rv->PC & 0x1) {\n"
#else
        "if (rv->PC & 0x3) {\n"
#endif
        "rv_except_insn_misaligned(rv, pc);\n"
        "}\n");
    return;
}

static void emit_jal(rv_buffer *buff, uint32_t insn, struct riscv_t *rv UNUSED)
{
    /* J-type decoding */
    const uint32_t rd = dec_rd(insn);
    const int32_t rel = dec_jtype_imm(insn);

    COMMENT("JAL");
    /* compute return address */
    CODE("ra = rv->PC + rv->insn_len;\n");
    CODE("pc = rv->PC;\n");
    CODE("rv->PC += %u;\n", rel);

    /* link */
    if (rd != rv_reg_zero)
        CODE("\trv->X[%u] = ra;\n", rd);

    CODE(
#ifdef ENABLE_RV32C
        "if (rv->PC & 0x1) {"
#else
        "if (rv->PC & 0x3) {"
#endif
        "rv_except_insn_misaligned(rv, pc);\n"
        "}");
}

static void emit_op_system(rv_buffer *buff, uint32_t insn, struct riscv_t *rv)
{
    /* I-type decoding */
    const int32_t imm = dec_itype_imm(insn);
    const int32_t csr = dec_csr(insn);
    const uint32_t funct3 = dec_funct3(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rd = dec_rd(insn);

    /* dispatch by func3 field */
    switch (funct3) {
    case 0:
        /* dispatch from imm field */
        switch (imm) {
        case 0: /* ECALL */
            COMMENT("ECALL");
            CODE("rv->io.on_ecall(rv);\n");
            goto update;
        case 1: /* EBREAK */
            COMMENT("EBREAK");
            CODE("rv->io.on_ebreak(rv);\n");
            goto update;
        case 0x002: /* URET */
        case 0x102: /* SRET */
        case 0x202: /* HRET */
        case 0x105: /* WFI */
            illegal_insn;
            return;
        case 0x302: /* MRET */
            COMMENT("MRET");
            UPDATE_PC(rv->csr_mepc);
            /* this is a branch */
            return;
        default:
            illegal_insn;
            return;
        }
        break;
#ifdef ENABLE_Zicsr
    case 1: { /* CSRRW    (Atomic Read/Write CSR) */
        if (rd != rv_reg_zero) {
            COMMENT("CSRRW");
            CODE("tmp_u32 = csr_csrrw(rv, %d, rv->X[%u]);\n", csr, rs1);
            CODE("rv->X[%u] = %u ? tmp_u32 : rv->X[%u];\n", rd, rd, rd);
        }
        goto update;
    }
    case 2: { /* CSRRS    (Atomic Read and Set Bits in CSR) */
        COMMENT("CSRRS");
        if (rs1 == rv_reg_zero)
            CODE("tmp_u32 = csr_csrrs(rv, %d, 0u);\n", csr);
        else
            CODE("tmp_u32 = csr_csrrs(rv, %d, rv->X[%u]);\n", csr, rs1);
        CODE("rv->X[%u] = %u ? tmp_u32 : rv->X[%u];\n", rd, rd, rd);
        goto update;
    }
    case 3: { /* CSRRC    (Atomic Read and Clear Bits in CSR) */
        COMMENT("CSRRC");
        if (rs1 == rv_reg_zero)
            CODE("tmp_u32 = csr_csrrc(rv, %d, ~0u);\n", csr);
        else
            CODE("tmp_u32 = csr_csrrc(rv, %d, rv->X[%u]);\n", csr, rs1);
        CODE("rv->X[%u] = %u ? tmp_u32 : rv->X[%u];\n", rd, rd, rd);
        goto update;
    }
    case 5: { /* CSRRWI */
        COMMENT("CSRRWI");
        CODE("tmp_u32 = csr_csrrw(rv, %d, %u);\n", csr, rs1);
        CODE("rv->X[%u] = %u ? tmp_u32 : rv->X[%u];\n", rd, rd, rd);
        goto update;
    }
    case 6: { /* CSRRSI */
        COMMENT("CSRRSI");
        CODE("tmp_u32 = csr_csrrs(rv, %d, %u);\n", csr, rs1);
        CODE("rv->X[%u] = %u ? tmp_u32 : rv->X[%u];\n", rd, rd, rd);
        goto update;
    }
    case 7: { /* CSRRCI */
        COMMENT("CSRRCI");
        CODE("tmp_u32 = csr_csrrc(rv, %d, %u);\n", csr, rs1);
        CODE("rv->X[%u] = %u ? tmp_u32 : rv->X[%u];\n", rd, rd, rd);
        goto update;
    }
#endif /* ENABLE_Zicsr */
    default:
        illegal_insn;
        return;
    }
update:
    CODE("rv->PC += rv->insn_len;\n");
    UPDATE_INSN32_LEN;
}

void emit_misc_mem(rv_buffer *buff, uint32_t insn UNUSED, struct riscv_t *rv)
{
#ifdef ENABLE_Zifencei
    UPDATE_PC(4);
    UPDATE_INSN32_LEN;
    UPDATE_PC(rv->insn_len);
#endif
}

static void jit_codegen(rv_buffer *buff,
                        char *func_name,
                        struct block_t *block,
                        struct riscv_t *rv)
{
    size_t tem_len = sizeof(template) - 1;
    strncpy(buff->src, template, tem_len);
    buff->size += tem_len;
    uint32_t *insns = block->code;
    DECLARE_FUNC(func_name);
    DECLARE_VAR;
#define emit(op) &&emit_##op

    void *emit_table[] = {
        [op_load] = emit(load),     [op_misc_mem] = emit(misc_mem),
        [op_op_imm] = emit(op_imm), [op_auipc] = emit(auipc),
        [op_store] = emit(store),   [op_amo] = emit(amo),
        [op_op] = emit(op),         [op_lui] = emit(lui),
        [op_branch] = emit(branch), [op_jalr] = emit(jalr),
        [op_jal] = emit(jal),       [_op_system] = emit(op_system),
    };
    uint32_t i = 0;
    uint32_t insn, index;

#define DISPATCH()                          \
    {                                       \
        if (i < block->instructions) {      \
            insn = insns[i++];              \
            index = (insn & INSN_6_2) >> 2; \
            goto *emit_table[index];        \
        } else {                            \
            END;                            \
            return;                         \
        }                                   \
    }
#define TARGET(op)                             \
    {                                          \
        emit_##op : emit_##op(buff, insn, rv); \
        DISPATCH();                            \
    }

    DISPATCH();

    TARGET(load);
    TARGET(misc_mem);
    TARGET(op_imm);
    TARGET(auipc);
    TARGET(store);
    TARGET(amo);
    TARGET(op);
    TARGET(lui);
    TARGET(branch);
    TARGET(jalr);
    TARGET(jal);
    TARGET(op_system);
}

static int getc_func(void *data)
{
    rv_buffer *buffer = data;
    return buffer->cur >= buffer->size ? EOF : buffer->src[buffer->cur++];
}

static void *import_solver(const char *name)
{
    for (int i = 0; imported_funcs[i].name; i++) {
        if (!strcmp(name, imported_funcs[i].name))
            return imported_funcs[i].func;
    }
    return NULL;
}

#define DLIST_ITEM_FOREACH(modules, item)                     \
    for (item = DLIST_HEAD(MIR_item_t, modules->items); item; \
         item = DLIST_NEXT(MIR_item_t, item))

#define DLIST_MODULE_FOREACH(ctx, module)                                      \
    for (module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx)); module; \
         module = DLIST_NEXT(MIR_module_t, module))

void decode(struct riscv_t *rv UNUSED,
            const uint32_t insn,
            const uint32_t insn_len,
            uint32_t *pc)
{
    const uint32_t index = (insn & INSN_6_2) >> 2;
    switch (index) {
    case op_branch:
    case op_jalr:
        break;
    case op_load:
    case op_op:
    case op_op_imm:
    case op_auipc:
    case op_lui:
    case _op_system:
        *pc += insn_len;
        break;
    case op_store:
        *pc += 4;
        break;
    case op_jal: {
        const int32_t rel = dec_jtype_imm(insn);
        *pc += rel;
        break;
    }
#ifdef ENABLE_RV32A
    case op_amo:
        *pc += 4;
        break;
#endif
#ifdef ENABLE_Zifencei
    case op_misc_mem:
        *pc += 4;
        break;
#endif
    }
}

/* allocate a block map */
struct block_map_t *block_map_alloc(uint32_t bits)
{
    struct block_map_t *map = malloc(sizeof(struct block_map_t));
    const uint32_t capacity = (1 << bits);
    void *ptr = calloc(capacity, sizeof(struct block_t *));
    map->bits = bits;
    map->map = (struct block_t **) ptr;
    map->capacity = capacity;
    map->size = 0;
    return map;
}

/* flush the instruction cache for a region */
static void rv_jit_clear(struct riscv_t *rv)
{
    struct block_map_t *map = rv->jit->block_map;
    for (uint32_t i = 0; i < map->capacity; i++) {
        struct block_t *block = map->map[i];
        free(block);
    }
}

/* free block map but not clear all entries */
static void block_map_free(struct block_map_t *map)
{
    assert(map->map);
    free(map->map);
    map->map = NULL;
}

/* clear all entries in the block map */
static void block_map_clear(struct block_map_t *map)
{
    assert(map->map);
    for (uint32_t i = 0; i < map->capacity; i++) {
        struct block_t *block = map->map[i];
        if (block) {
            free(block->code);
            free(block);
            map->map[i] = NULL;
        }
    }
    map->size = 0;
}

/* insert a block into a blockmap */
static void block_map_insert(struct block_map_t *map, struct block_t *block)
{
    assert(map->map && block);
    /* insert into the block map */
    const uint32_t mask = map->capacity - 1;
    uint32_t index = hash(block->pc_start);
    for (;; ++index) {
        if (map->map[index & mask] == NULL) {
            map->map[index & mask] = block;
            break;
        }
    }
    ++map->size;
}

/* try to locate an already translated block in the block map */
static struct block_t *block_find(struct block_map_t *map, uint32_t addr)
{
    assert(map);
    uint32_t index = hash(addr);
    const uint32_t mask = map->capacity - 1;
    for (;; ++index) {
        struct block_t *block = map->map[index & mask];
        if (!block)
            return NULL;

        if (block->pc_start == addr)
            return block;
    }
    return NULL;
}

/* expand block_map capacity 2 times and reinsert entries */
static void block_map_enlarge(struct riscv_jit_t *jit)
{
    struct block_map_t *old_map = jit->block_map;
    struct block_map_t *new_map = block_map_alloc(old_map->bits + 1);

    for (size_t i = 0; i < old_map->capacity; i++) {
        struct block_t *block = old_map->map[i];
        if (block)
            block_map_insert(new_map, block);
    }

    block_map_free(old_map);
    jit->block_map = new_map;
}

static void rv_translate_block(struct riscv_t *rv, struct block_t *block)
{
    assert(rv && block);
    /* setup the basic block */
    block->instructions = 0;
    block->pc_start = rv->PC;
    block->pc_end = rv->PC;

    /* translate the basic block */
    for (;;) {
        /* fetch the next instruction */
        const uint32_t insn = rv->io.mem_ifetch(rv, block->pc_end);
        const uint32_t index = (insn & INSN_6_2) >> 2;

        decode(rv, insn, rv->jit->insn_len, &block->pc_end);
        rv->jit->insn_len = INSN_32;
        uint32_t pc = block->pc_end;

        if (block->instructions >= block->code_capacity) {
            break;
            block->code_capacity += 20;
            block->code =
                realloc(block->code, sizeof(uint32_t *) * block->code_capacity);
        }
        block->code[block->instructions++] = insn;
        block->pc_end = pc;
        /* stop on branch and jalr */
        if (index == op_branch || index == op_jalr) {
            break;
        }
    }
}

static inline void dump_code(rv_buffer *buffer,
                             size_t start,
                             struct riscv_jit_t *jit)
{
    fwrite(buffer->src + start - 1, buffer->size - start, 1, jit->code_log);
}

static void block_finish(struct riscv_t *rv, struct block_t *block)
{
    struct riscv_jit_t *jit = rv->jit;
    c2mir_init(jit->ctx);
    size_t gen_num = 0;
    MIR_gen_init(jit->ctx, gen_num);
    MIR_gen_set_optimize_level(jit->ctx, gen_num, 3);
    if (jit_config->report) {
        MIR_gen_set_debug_level(jit->ctx, 0, 1);
        MIR_gen_set_debug_file(jit->ctx, 0, jit->codegen_log);
    }

    char func_name[25];
    snprintf(func_name, 25, "jit_func_%d_%d", block->pc_start,
             block->instructions);

    size_t tem_size = sizeof(template) - 1;
    const size_t n = tem_size + 4096;
    rv_buffer *buffer = malloc(sizeof(rv_buffer));
    buffer->src = (char *) malloc(n);
    buffer->capacity = n;
    buffer->size = 0;
    buffer->cur = 0;

    jit_codegen(buffer, func_name, block, rv);
    int ret = c2mir_compile(jit->ctx, jit->options, getc_func, buffer,
                            func_name, NULL);
    assert(ret);
    if (jit->code_log)
        dump_code(buffer, tem_size, jit);
    free(buffer->src);
    free(buffer);

    MIR_module_t module =
        DLIST_TAIL(MIR_module_t, *MIR_get_module_list(jit->ctx));
    MIR_load_module(jit->ctx, module);
    MIR_link(jit->ctx, MIR_set_gen_interface, import_solver);

    MIR_item_t mir_func;
    DLIST_ITEM_FOREACH (module, mir_func) {
        if (mir_func->item_type == MIR_func_item &&
            !strcmp(mir_func->u.func->name, func_name)) {
            break;
        }
    }
    assert(mir_func);
    block->func = MIR_gen(jit->ctx, gen_num, mir_func);

    MIR_gen_finish(jit->ctx);
    c2mir_finish(jit->ctx);

    /* insert new block into block map */
    block_map_insert(jit->block_map, block);
}

/* allocate a new code block */
static struct block_t *block_alloc()
{
    /* place a new block */
    struct block_t *block = (struct block_t *) malloc(sizeof(struct block_t));
    /* set the initial code generation write head */
    memset(block, 0, sizeof(struct block_t));
    block->code_capacity = 50;
    block->code = malloc(sizeof(uint32_t) * block->code_capacity);
    return block;
}

struct block_t *block_find_or_translate(struct riscv_t *rv,
                                        struct block_t *prev)
{
    /* lookup the next block in the block map */
    struct block_t *next = block_find(rv->jit->block_map, rv->PC);
    struct block_map_t *block_map = rv->jit->block_map;

    /* translate if we did not find one */
    if (!next) {
        if (block_map->size * 1.25 > block_map->capacity) {
            block_map_clear(block_map);
            prev = NULL;
        }

        next = block_alloc();
        assert(next);
        rv_translate_block(rv, next);
        block_finish(rv, next);

        /* update the block predictor
         *
         * If the block predictor gives us a win when we
         * translate a new block but gives us a huge penalty when
         * updated after we find a new block.  did not expect that.
         */
        if (prev)
            prev->predict = next;
    }
    assert(next);
    return next;
}

static void rv_jit_load_cache(struct riscv_jit_t *jit, const char *cache)
{
    MIR_context_t ctx = jit->ctx;
    FILE *file = fopen(cache, "rb");
    if (!file)
        return;
    MIR_read(ctx, file);
    fclose(file);

    c2mir_init(ctx);
    MIR_gen_init(ctx, 5);
    MIR_gen_set_optimize_level(ctx, 0, 3);

    MIR_module_t all_modules =
        DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
    size_t module_size = DLIST_LENGTH(MIR_module_t, all_modules);
    struct block_map_t *map = jit->block_map;
    if (!module_size)
        return;

    MIR_module_t modules;
    MIR_item_t mir_func;
    char *delim = "_";
    DLIST_MODULE_FOREACH (ctx, modules) {
        DLIST_ITEM_FOREACH (modules, mir_func) {
            if (mir_func->item_type == MIR_func_item &&
                strstr(mir_func->u.func->name, "jit_func")) {
                MIR_load_module(ctx, modules);
                MIR_link(ctx, MIR_set_gen_interface, import_solver);
                struct block_t *block = block_alloc();

                char *op_name = mir_func->u.func->name;
                op_name = op_name + 9;
                char *val = strtok(op_name, delim);
                block->pc_start = atoi(val);
                val = strtok(NULL, delim);
                block->instructions = atoi(val);

                block->func = mir_func->addr;
                block_map_insert(map, block);
            }
        }
    }
}

struct jit_config_t *jit_config_init()
{
    struct jit_config_t *obj = malloc(sizeof(struct jit_config_t));
    memset(obj, 0, sizeof(struct jit_config_t));
    return obj;
}

void jit_set_file_name(struct jit_config_t *config, const char *opt_prog_name)
{
    size_t len = 0, opt_len = strlen(opt_prog_name);
    for (; len < opt_len; len++) {
        if (opt_prog_name[len] == '.')
            break;
    }

    config->program = malloc(len + 1);
    strncpy(config->program, opt_prog_name, len);
    config->program[len] = 0;
}

static void blocks_save(struct riscv_jit_t *jit)
{
    size_t len = strlen(jit_config->program);
    char cache[len + 5];
    snprintf(cache, 30, "%s.mirb", jit_config->program);
    jit->cache = fopen(cache, "wb");
    MIR_write(jit->ctx, jit->cache);
    fclose(jit->cache);
}

struct riscv_jit_t *rv_jit_init(uint32_t bits)
{
    struct riscv_jit_t *jit =
        (struct riscv_jit_t *) malloc(sizeof(struct riscv_jit_t));
    memset(jit, 0, sizeof(struct riscv_jit_t));
    jit->options = malloc(sizeof(struct c2mir_options));
    memset(jit->options, 0, sizeof(struct c2mir_options));
    jit->ctx = MIR_init();
    jit->block_map = block_map_alloc(bits);
    size_t len = strlen(jit_config->program);
    char cache[len + 5];
    snprintf(cache, 30, "%s.mirb", jit_config->program);
    rv_jit_load_cache(jit, cache);

    if (jit_config->report) {
        char report[len + 4];
        snprintf(report, 30, "%s.log", jit_config->program);
        jit->codegen_log = fopen(report, "w");
        jit->code_log = fopen("codegen.c", "w");
    }
    jit_config->jit = jit;
    return jit;
}

void rv_jit_free(struct riscv_jit_t *jit)
{
    if (jit->block_map) {
        block_map_clear(jit->block_map);
        block_map_free(jit->block_map);
    }

    MIR_context_t ctx = jit->ctx;
    if (jit_config->cache) {
        blocks_save(jit);
    }

    if (jit_config->report) {
        fclose(jit->codegen_log);
        fclose(jit->code_log);
    }

    MIR_finish(ctx);
    free(jit_config->program);
    free(jit_config);
    free(jit->options);
    free(jit);
}

void jit_handler(int sig UNUSED)
{
    struct riscv_jit_t *jit = jit_config->jit;
    if (jit_config->cache)
        blocks_save(jit);
}