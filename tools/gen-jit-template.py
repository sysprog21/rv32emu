#!/usr/bin/env python3

'''
This script serves as a code generator for creating JIT code templates
based on existing code files in the 'src' directory, eliminating the need
for writing duplicated code.
'''

import re
import sys

INSN = {
    "Zifencei": ["fencei"],
    "Zicsr": [
        "csrrw",
        "csrrs",
        "csrrc",
        "csrrw",
        "csrrsi",
        "csrrci"],
    "EXT_M": [
        "mul",
        "mulh",
        "mulhsu",
        "mulhu",
        "div",
        "divu",
        "rem",
        "remu"],
    "EXT_A": [
        "lrw",
        "scw",
        "amoswapw",
        "amoaddw",
        "amoxorw",
        "amoandw",
        "amoorw",
        "amominw",
        "amomaxw",
        "amominuw",
        "amomaxuw"],
    "EXT_F": [
        "flw",
        "fsw",
        "fmadds",
        "fmsubs",
        "fnmsubs",
        "fnmadds",
        "fadds",
        "fsubs",
        "fmuls",
        "fdivs",
        "fsqrts",
        "fsgnjs",
        "fsgnjns",
        "fsgnjxs",
        "fmins",
        "fmaxs",
        "fcvtws",
        "fcvtwus",
        "fmvxw",
        "feqs",
        "flts",
        "fles",
        "fclasss",
        "fcvtsw",
        "fcvtswu",
        "fmvwx"],
    "EXT_C": [
        "caddi4spn",
        "clw",
        "csw",
        "cnop",
        "caddi",
        "cjal",
        "cli",
        "caddi16sp",
        "clui",
        "csrli",
        "csrai",
        "candi",
        "csub",
        "cxor",
        "cor",
        "cand",
        "cj",
        "cbeqz",
        "cbnez",
        "cslli",
        "clwsp",
        "cjr",
        "cmv",
        "cebreak",
        "cjalr",
        "cadd",
        "cswsp",
    ],
}
EXT_LIST = ["Zifencei", "Zicsr", "EXT_M", "EXT_A", "EXT_F", "EXT_C"]
SKIP_LIST = []
# check enabled extension in Makefile


def parse_argv(EXT_LIST, SKIP_LIST):
    for argv in sys.argv:
        if argv.find("RV32_FEATURE_") != -1:
            ext = argv[argv.find("RV32_FEATURE_") + 13:-2]
            if argv[-1:] == "1" and EXT_LIST.count(ext):
                EXT_LIST.remove(ext)
    for ext in EXT_LIST:
        SKIP_LIST += INSN[ext]


def remove_comment(str):
    str = re.sub(r'//[\s|\S]+?\n', "", str)
    return re.sub(r'/\*[\s|\S]+?\*/\n', "", str)


# parse_argv(EXT_LIST, SKIP_LIST)
# prepare PROLOGUE
output = ""
f = open('src/rv32_template.c', 'r')
lines = f.read()
# remove exception handler
lines = re.sub(r'RV_EXC[\S]+?\([\S|\s]+?\);\s', "", lines)
# collect functions
emulate_funcs = re.findall(r'RVOP\([\s|\S]+?}\)', lines)
codegen_funcs = re.findall(r'X64\([\s|\S]+?}\)', lines)

op = []
impl = []
for i in range(len(emulate_funcs)):
    op.append(emulate_funcs[i][5:emulate_funcs[i].find(',')])
    impl.append(codegen_funcs[i])

f.close()

fields = {"imm", "pc", "rs1", "rs2", "rd", "shamt", "branch_taken", "branch_untaken"}
# generate jit template
for i in range(len(op)):
    if (not SKIP_LIST.count(op[i])):
        output += impl[i][0:4] + op[i] + ", {"
        IRs = re.findall(r'[\s|\S]+?;', impl[i][5:])
        # parse_and_translate_IRs
        for i in range(len(IRs)):
            IR = IRs[i].strip()[:-1]
            items = [s.strip() for s in IR.split(',')]
            asm = ""
            for i in range(len(items)):
                if items[i] in fields:
                    items[i] = "ir->" + items[i]
            if items[0] == "alu32_imm":
                if len(items) == 8:
                    asm = "emit_alu32_imm{}(state, {}, {}, {}, ({}{}_t) {});".format(
                        items[1], items[2], items[3], items[4], items[5], items[6], items[7])
                elif len(items) == 7:
                    asm = "emit_alu32_imm{}(state, {}, {}, {}, {} & {});".format(
                        items[1], items[2], items[3], items[4], items[5], items[6])
                else:
                    asm = "emit_alu32_imm{}(state, {}, {}, {}, {});".format(
                        items[1], items[2], items[3], items[4], items[5])
            elif items[0] == "alu64_imm":
                asm = "emit_alu64_imm{}(state, {}, {}, {}, {});".format(
                    items[1], items[2], items[3], items[4], items[5])
            elif items[0] == "alu64":
                asm = "emit_alu64(state, {}, {}, {});".format(
                    items[1], items[2], items[3])
            elif items[0] == "alu32":
                asm = "emit_alu32(state, {}, {}, {});".format(
                    items[1], items[2], items[3])
            elif items[0] == "ld_imm":
                if items[2] == "mem":
                    asm = "emit_load_imm(state, {}, (intptr_t) (m->mem_base + ir->imm));".format(
                        items[1])
                elif len(items) == 4:
                    asm = "emit_load_imm(state, {}, {} + {});".format(
                        items[1], items[2], items[3])
                else:
                    asm = "emit_load_imm(state, {}, {});".format(
                        items[1], items[2])
            elif items[0] == "ld_sext":
                if (items[3] == "X"):
                    asm = "emit_load_sext(state, {}, parameter_reg[0], {}, offsetof(struct riscv_internal, X) + 4 * {});".format(
                        items[1], items[2], items[4])
                else:
                    asm = "emit_load_sext(state, {}, {}, {}, {});".format(
                    items[1], items[2],  items[3],  items[4])
            elif items[0] == "ld":
                if (items[3] == "X"):
                    asm = "emit_load(state, {}, parameter_reg[0], {}, offsetof(struct riscv_internal, X) + 4 * {});".format(
                        items[1], items[2], items[4])
                else:
                    asm = "emit_load(state, {}, {}, {}, {});".format(
                        items[1], items[2], items[3], items[4])
            elif items[0] == "st_imm":
                asm = "emit_store_imm32(state, {}, parameter_reg[0], offsetof(struct riscv_internal, X) + 4 * {}, {});".format(
                    items[1], items[2], items[3])
            elif items[0] == "st":
                if (items[3] == "X"):
                    asm = "emit_store(state, {}, {}, parameter_reg[0], offsetof(struct riscv_internal, X) + 4 * {});".format(
                        items[1], items[2], items[4])
                elif items[3] == "PC" or items[3] == "compressed":
                    asm = "emit_store(state, {}, {}, parameter_reg[0], offsetof(struct riscv_internal, {}));".format(
                        items[1], items[2], items[3])
                else:
                    asm = "emit_store(state, {}, {}, {}, {});".format(
                        items[1], items[2], items[3], items[4])
            elif items[0] == "cmp":
                asm = "emit_cmp32(state, {}, {});".format(
                    items[1], items[2])
            elif items[0] == "cmp_imm":
                asm = "emit_cmp_imm32(state, {}, {});".format(
                    items[1], items[2])
            elif items[0] == "jmp":
                asm = "emit_jmp(state, {} + {});".format(
                    items[1], items[2])
            elif items[0] == "jcc":
                asm = "emit_jcc_offset(state, {});".format(items[1])
            elif items[0] == "set_jmp_off":
                asm = "uint32_t jump_loc = state->offset;"
            elif items[0] == "jmp_off":
                asm = "emit_jump_target_offset(state, jump_loc + 2, state->offset);"
            elif items[0] == "mem":
                asm = "memory_t *m = ((state_t *) rv->userdata)->mem;"
            elif items[0] == "call":
                asm = "emit_call(state, (intptr_t) rv->io.on_{});".format(
                    items[1])
            elif items[0] == "exit":
                asm = "emit_exit(&(*state));"
            elif items[0] == "mul":
                asm = "muldivmod(state, {}, {}, {}, {});".format(
                    items[1], items[2], items[3], items[4])
            elif items[0] == "div":
                asm = "muldivmod(state, {}, {}, {}, {});".format(
                    items[1], items[2], items[3], items[4])
            elif items[0] == "mod":
                asm = "muldivmod(state, {}, {}, {}, {});".format(
                    items[1], items[2], items[3], items[4])
            elif items[0] == "cond":
                asm = "if({})".format(items[1]) + "{"
            elif items[0] == "end":
                asm = "}"
            elif items[0] == "assert":
                asm = "assert(NULL);"
            output += asm + "\n"
        output += "})\n"

sys.stdout.write(output)
