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
    "EXT_FC": [
        "cflwsp",
        "cfswsp",
        "cflw",
        "cfsw",
    ],
    "SYSTEM": ["sret"],
    "Zba": [
        "sh3add",
        "sh2add",
        "sh1add",
    ],
    "Zbb": [
        "rev8",
        "orcb",
        "rori",
        "ror",
        "rol",
        "zexth",
        "sexth",
        "sextb",
        "minu",
        "maxu",
        "min",
        "max",
        "cpop",
        "ctz",
        "clz",
        "xnor",
        "orn",
        "andn",
    ],
    "Zbc": [
        "clmulr",
        "clmulh",
        "clmul",
    ],
    "Zbs": [
        "bseti",
        "bset",
        "binvi",
        "binv",
        "bexti",
        "bext",
        "bclri",
        "bclr",
    ],
}
EXT_LIST = ["Zifencei", "Zicsr", "EXT_M", "EXT_A", "EXT_F", "EXT_C", "SYSTEM", "Zba", "Zbb", "Zbc", "Zbs"]
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
    if "EXT_F" in EXT_LIST or "EXT_C" in EXT_LIST:
        SKIP_LIST += INSN["EXT_FC"]

parse_argv(EXT_LIST, SKIP_LIST)
# prepare PROLOGUE
output = ""
f = open('src/rv32_template.c', 'r')
lines = f.read()
# remove_comment
lines = re.sub(r'/\*[\s|\S]+?\*/', "", lines)
# remove exception handler
lines = re.sub(r'RV_EXC[\S]+?\([\S|\s]+?\);\s', "", lines)
# collect functions
emulate_funcs = re.findall(r'RVOP\([\s|\S]+?}\)', lines)
codegen_funcs = re.findall(r'GEN\([\s|\S]+?}\)', lines)
op = []
impl = []
for i in range(len(emulate_funcs)):
    op.append(emulate_funcs[i][5:emulate_funcs[i].find(',')].strip())
    impl.append(codegen_funcs[i])

f.close()

fields = {"imm", "pc", "rs1", "rs2", "rd", "shamt", "branch_taken", "branch_untaken"}
virt_regs = {"VR0", "VR1", "VR2"}
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
                if items[i] in virt_regs:
                    items[i] = "vm_reg[" + items[i][-1] + "]"
                if items[i] == "TMP":
                    items[i] = "temp_reg"   
            if items[0] == "alu32imm":
                if len(items) == 8:
                    asm = "emit_alu32_imm{}(state, {}, {}, {}, ({}{}_t) {});".format(
                        items[1], items[2], items[3], items[4], items[5], items[6], items[7])
                elif len(items) == 7:
                    asm = "emit_alu32_imm{}(state, {}, {}, {}, {} & {});".format(
                        items[1], items[2], items[3], items[4], items[5], items[6])
                else:
                    asm = "emit_alu32_imm{}(state, {}, {}, {}, {});".format(
                        items[1], items[2], items[3], items[4], items[5])
            elif items[0] == "alu64imm":
                asm = "emit_alu64_imm{}(state, {}, {}, {}, {});".format(
                    items[1], items[2], items[3], items[4], items[5])
            elif items[0] == "alu64":
                asm = "emit_alu64(state, {}, {}, {});".format(
                    items[1], items[2], items[3])
            elif items[0] == "alu32":
                asm = "emit_alu32(state, {}, {}, {});".format(
                    items[1], items[2], items[3])
            elif items[0] == "ldimm":
                if items[2] == "mem":
                    asm = "emit_load_imm(state, {}, (intptr_t) (m->mem_base + ir->imm));".format(
                        items[1])
                elif len(items) == 4:
                    asm = "emit_load_imm(state, {}, {} + {});".format(
                        items[1], items[2], items[3])
                else:
                    asm = "emit_load_imm(state, {}, {});".format(
                        items[1], items[2])
            elif items[0] == "lds":
                if (items[3] == "X"):
                    asm = "emit_load_sext(state, {}, parameter_reg[0], {}, offsetof(riscv_t, X) + 4 * {});".format(
                        items[1], items[2], items[4])
                else:
                    asm = "emit_load_sext(state, {}, {}, {}, {});".format(
                    items[1], items[2],  items[3],  items[4])
            elif items[0] == "rald":
                asm = "{} = ra_load(state, {});".format(items[1], items[2])
            elif items[0] == "rald2":
                asm = "ra_load2(state, {}, {});".format(items[1], items[2])
            elif items[0] == "rald2s":
                asm = "ra_load2_sext(state, {}, {}, {}, {});".format(items[1], items[2], items[3], items[4])
            elif items[0] == "map":
                asm = "{} = map_vm_reg(state, {});".format(items[1], items[2])
            elif items[0] == "ld":
                if (items[3] == "X"):
                    asm = "emit_load(state, {}, parameter_reg[0], {}, offsetof(riscv_t, X) + 4 * {});".format(
                        items[1], items[2], items[4])
                else:
                    asm = "emit_load(state, {}, {}, {}, {});".format(
                        items[1], items[2], items[3], items[4])
            elif items[0] == "st":
                if (items[3] == "X"):
                    asm = "emit_store(state, {}, {}, parameter_reg[0], offsetof(riscv_t, X) + 4 * {});".format(
                        items[1], items[2], items[4])
                elif items[3] == "PC" or items[3] == "compressed":
                    asm = "emit_store(state, {}, {}, parameter_reg[0], offsetof(riscv_t, {}));".format(
                        items[1], items[2], items[3])
                else:
                    asm = "emit_store(state, {}, {}, {}, {});".format(
                        items[1], items[2], items[3], items[4])
            elif items[0] == "mov":
                asm = "emit_mov(state, {}, {});".format(
                    items[1], items[2])
            elif items[0] == "cmp":
                asm = "emit_cmp32(state, {}, {});".format(
                    items[1], items[2])
            elif items[0] == "cmpimm":
                asm = "emit_cmp_imm32(state, {}, {});".format(
                    items[1], items[2])
            elif items[0] == "jmp":
                asm = "emit_jmp(state, {} + {});".format(
                    items[1], items[2])
            elif items[0] == "jcc":
                asm = "emit_jcc_offset(state, {});".format(items[1])
            elif items[0] == "setjmpoff":
                asm = "uint32_t jump_loc = state->offset;"
            elif items[0] == "jmpoff":
                asm = "emit_jump_target_offset(state, JUMP_LOC, state->offset);"
            elif items[0] == "mem":
                asm = "memory_t *m = PRIV(rv)->mem;"
            elif items[0] == "call":
                asm = "emit_call(state, (intptr_t) rv->io.on_{});".format(
                    items[1])
            elif items[0] == "exit":
                asm = "emit_exit(state);"
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
                if items[1] == "regneq":
                    items[1] = "vm_reg[0] != vm_reg[1]"
                asm = "if({})".format(items[1]) + "{"
            elif items[0] == "else":
                asm = "} else {"
            elif items[0] == "end":
                asm = "}"
            elif items[0] == "pollute":
                asm = "set_dirty({}, true);".format(items[1])
            elif items[0] == "break":
                asm = "store_back(state);"
            elif items[0] == "assert":
                asm = "assert(NULL);"
            elif items[0] == "predict":
                asm = "parse_branch_history_table(state, ir);"
            output += asm + "\n"
        output += "})\n"

sys.stdout.write(output)
