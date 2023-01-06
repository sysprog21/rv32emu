/*
 * Code Generation for RV32IM
 *
 * Copyright (c) 2020 Jörg Mische <bobbl@gmx.de>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* constants */
unsigned buf_size;

/* global variables */
unsigned char *buf;
unsigned code_pos;
unsigned syms_head;
unsigned stack_pos;
unsigned num_params;
unsigned reg_pos;
unsigned global_pos;
unsigned last_insn;

void set_32bit(unsigned char *p, unsigned x);
unsigned get_32bit(unsigned char *p);

void emit(unsigned b)
{
    buf[code_pos] = b;
    code_pos = code_pos + 1;
}

void emit_multi(unsigned n, char *s)
{
    unsigned i = 0;
    while (i < n) {
        emit(s[i]);
        i = i + 1;
    }
}

void emit32(unsigned n)
{
    emit(n);
    emit(n >> 8);
    emit(n >> 16);
    emit(n >> 24);
    last_insn = n;
}

unsigned insn_jal(unsigned rd, unsigned immj)
{
    return ((immj & 1048576) << 11) | /* bit  31     = imm[20] */
           ((immj & 2046) << 20) |    /* bits 30..21 = imm[10..1] */
           ((immj & 2048) << 9) |     /* bit  20     = imm[11] */
           ((immj & 1044480)) |       /* bits 19..12 = imm[19..12] */
           (rd << 7) |                /* bits 11..7  = rd */
           111;                       /* bits  6..0  = 0x6f (jal) */
}

void emit_insn_sw(unsigned rs2, unsigned rs1, unsigned immi)
{
    emit32(((immi & 4064) << 20) | /* bits 31..25 = imm[11..5] */
           (rs2 << 20) |           /* bits 24..20 = rs2 */
           (rs1 << 15) |           /* bits 19..15 = rs1 */
           ((immi & 31) << 7) |    /* bits 11..7  = imm[4..0] */
           8227);                  /* bits 14..12 */
                                   /* bits  6..0  = 0x2023 (sw) */
}

void emit_insn_lw(unsigned rd, unsigned rs, unsigned immi)
{
    emit32((immi << 20) | /* bits 31..20 = imm[11..0] */
           (rs << 15) |   /* bits 19..15 = rs */
           (rd << 7) |    /* bits 11..7  = rd */
           8195);         /* bits 14..12 */
                          /* bits  6..0  = 0x2003 (lw) */
}

void emit_insn_addsp(unsigned n)
{
    if (n)
        emit32(65811 + (n << 22));
}

void emit_insn_d_s_t1(unsigned opcode)
{
    emit32(opcode + ((reg_pos + 10) << 7) + ((reg_pos + 10) << 15) +
           ((reg_pos + 11) << 20));
}

void emit_insn_d_s1_t(unsigned opcode)
/* used only once */
{
    emit32(opcode + ((reg_pos + 10) << 7) + ((reg_pos + 11) << 15) +
           ((reg_pos + 10) << 20));
}

void emit_insn_d_s(unsigned opcode)
{
    emit32(opcode + ((reg_pos + 10) << 7) + ((reg_pos + 10) << 15));
}

void emit_insn_d_t(unsigned opcode)
{
    emit32(opcode + ((reg_pos + 10) << 7) + ((reg_pos + 10) << 20));
}

void emit_insn_d(unsigned opcode)
{
    emit32(opcode + ((reg_pos + 10) << 7));
}

void emit_insn_s_t1(unsigned opcode)
/* used only once */
{
    emit32(opcode + ((reg_pos + 10) << 15) + ((reg_pos + 11) << 20));
}

void emit_push()
{
    reg_pos = reg_pos + 1;
}

unsigned emit_scope_begin()
{
    return stack_pos;
}

void emit_scope_end(unsigned save)
{
    emit_insn_addsp(stack_pos - save);
    stack_pos = save;
}

void emit_number(unsigned imm)
{
    if ((imm + 2048) < 4096) {
        emit_insn_d(19 + (imm << 20));
        /* 00000013  addi REG, x0, IMM */
    } else {
        emit_insn_d(55 + (((imm + 2048) >> 12) << 12));
        /* 00000037  lui REG, IMM */
        if ((imm << 20)) {
            emit_insn_d_s(19 + (imm << 20));
            /* 00000013  addi REG, REG, IMM */
        }
    }
}

void emit_string(unsigned len, char *s)
{
    emit32(insn_jal(reg_pos + 10, (len + 8) & 4294967292));
    /* jal REGPOS, end_of_string */
    emit_multi(len, s);
    emit_multi(4 - (len & 3), "\x00\x00\x00\x00");
    /* at least one 0 as end mark and then align */
}

unsigned local_ofs(unsigned global, unsigned ofs)
{
    if (global == 0)
        ofs = (stack_pos + ofs) << 2;
    return ofs;
}

void emit_store(unsigned global, unsigned ofs)
{
    emit_insn_sw(reg_pos + 10, 2 + global, local_ofs(global, ofs));
}

void emit_load(unsigned global, unsigned ofs)
{
    emit_insn_lw(reg_pos + 10, 2 + global, local_ofs(global, ofs));
}

void emit_index(unsigned global, unsigned ofs)
{
    emit_insn_lw(reg_pos + 11, 2 + global, local_ofs(global, ofs));
    emit_insn_d_s_t1(51);
    /* add REGPOS, REGPOS, REGPOS+1 */
}

void emit_pop_store_array()
{
    reg_pos = reg_pos - 1;
    emit_insn_s_t1(35);
    /* 00000023  sb REG+1, 0(REG) */
}

void emit_load_array()
{
    emit_insn_d_s(16387);
    /* lbu REG, 0(REG) */
}

unsigned emit_pre_call()
/* save temporary registers and return how many */
{
    unsigned r = reg_pos;
    if (r) {
        /* save currently used temporary registers */
        emit_insn_addsp(0 - r);
        stack_pos = stack_pos + r;
        unsigned i = 0;
        while (i < r) {
            emit_insn_sw(i + 10, 2, i << 2);
            i = i + 1;
        }
    }
    reg_pos = 0;
    return r;
}

void emit_arg(unsigned i)
{
    reg_pos = reg_pos + 1;
}

void emit_call(unsigned defined,
               unsigned sym,
               unsigned ofs,
               unsigned pop,
               unsigned save)
{
    if (defined) { /* defined function */
        emit32(insn_jal(1, ofs - code_pos));
    } else { /* undefined function */
        set_32bit(buf + sym, code_pos);
        emit32(ofs);
    }
    reg_pos = save;

    if (reg_pos != 0) {
        /* restore previously saved temporary registers */
        emit_insn_d(327699);
        /* 000500513  mv REG, a0 */
        unsigned i = 0;
        while (i < reg_pos) {
            emit_insn_lw(i + 10, 2, i << 2);
            i = i + 1;
        }
        emit_insn_addsp(reg_pos);
        stack_pos = stack_pos - reg_pos;
    }
}

unsigned emit_fix_call_here(unsigned pos)
{
    unsigned next = get_32bit(buf + pos);
    set_32bit(buf + pos, insn_jal(1, code_pos - pos));
    return next;
}

void emit_operation(unsigned t)
{
    reg_pos = reg_pos - 1;

    unsigned o;
    if (t == 1)
        o = 4147; /* << sll */
    else if (t == 2)
        o = 20531; /* >> srl */
    else if (t == 3)
        o = 1073741875; /* -  sub */
    else if (t == 4)
        o = 24627; /* |  or  */
    else if (t == 5)
        o = 16435; /* ^  xor */
    else if (t == 6)
        o = 51; /* +  add */
    else if (t == 7)
        o = 28723; /* &  and */
    else if (t == 8)
        o = 33554483; /* *  mul */
    else if (t == 9)
        o = 33574963; /* / divu */
    else if (t == 10)
        o = 33583155; /* % remu */

    /* code optimization for constant immediates */
    if (((last_insn & 1044607) == 19) & (t < 8)) {
        /* 0xFF07F  addi ?, x0, ?
           register need not be checked
           if (((last_insn & 1048575) == (19 + ((reg_pos + 11) << 7))) { */
        unsigned imm = (last_insn >> 20) << 20;
        code_pos = code_pos - 4;
        if (t == 3) {
            emit_insn_d_s(19 + (0 - imm));
            /* 00000013  addi REG, REG, -IMM
               imm is always positive, therefore -(-2048)=2048
               cannot happen */
        } else
            emit_insn_d_s((o ^ 32) | imm);
    } else
        emit_insn_d_s_t1(o);
}

void emit_comp(unsigned t)
{
    reg_pos = reg_pos - 1;

    if (t < 18) {
        if ((last_insn & 4294963327) == 19) {
            /* 0xFFFFF07F optimization if compared with 0 */
            code_pos = code_pos - 4;
        } else {
            emit_insn_d_s_t1(1073741875);
            /* sub REG, REG, REG+1 */
        }
        if (t == 16)
            emit_insn_d_s(1060883);
        /* sltiu REG, REG, 1        == */
        else
            emit_insn_d_t(12339);
        /* sltu REG, x0, REG        != */
    } else {
        if (t < 20)
            emit_insn_d_s_t1(12339);
        /* sltu REG, REG, REG+1     < or >= */
        else
            emit_insn_d_s1_t(12339);
        /* sltu REG, REG+1, REG     > or <= */
        if (t & 1)
            emit_insn_d_s(1064979);
        /* xori REG, REG, 1         >= or <= */
    }
}

unsigned emit_branch_if0()
{
    /* reg_pos must be 0 */
    emit32(327779);
    return code_pos - 4;
    /* 00050063     beqz a0, ... */
}

unsigned emit_branch_if_cond(unsigned t)
{
    /* optimization: can be replaced by
     *   emit_comp(t);
     *   return emit_branch_if0();
     */

    /* reg_pos must be 1 */
    reg_pos = 0;

    unsigned o = 10874979;
    if (t < 18) {
        o = 11866211;
        if (last_insn == 1427) { /* 00000593  addi a1, x0, 0 */
            /* optimization if compared with 0 */
            code_pos = code_pos - 4;
            o = 331875; /* 00051063  bnez a0, +0 */
        }
    } else if (t < 20)
        o = 11890787;
    if (t & 1)
        o = o - 4096;
    emit32(o);

    /* t==16 00B51063     bne a0, a1, +0    ==
       t==17 00B50063     beq a0, a1, +0    !=
       t==18 00B57063     bgeu a0, a1, +0   <
       t==19 00B56063     bltu a0, a1, +0   >=
       t==20 00A5F063     bgeu a1, a0, +0   >
       t==21 00A5E063     bltu a1, a0, +0   <=
     */

    return code_pos - 4;
}

void emit_fix_branch_here(unsigned insn_pos)
{
    unsigned immb = code_pos - insn_pos;
    immb = ((immb & 4096) << 19) | /* bit  31     = immb[12] */
           ((immb & 2016) << 20) | /* bits 30..25 = immb[10..5] */
           ((immb & 30) << 7) |    /* bits 11..8  = immb[4..1] */
           ((immb & 2048) >> 4);   /* bit  7      = immb[11] */
    set_32bit(buf + insn_pos, get_32bit(buf + insn_pos) | immb);
}

void emit_fix_jump_here(unsigned insn_pos)
{
    set_32bit(buf + insn_pos, insn_jal(0, code_pos - insn_pos));
}

unsigned emit_jump(unsigned destination)
{
    emit32(insn_jal(0, destination - code_pos));
    return code_pos - 4;
}

void emit_enter(unsigned n)
{
    reg_pos = 0;
    stack_pos = 0;
    num_params = n;
    emit_insn_addsp(0 - n - 1);
    emit32(1122339);
    /* 00112023  sw ra, 0(sp) */
    unsigned i = 0;
    while (i < n) {
        emit_insn_sw(i + 10, 2, (i + 1) << 2);
        i = i + 1;
    }
}

void emit_return()
{
    emit_insn_lw(1, 2, stack_pos << 2);
    emit_insn_addsp(stack_pos + num_params + 1);
    emit32(32871); /* 00008067 ret */
}

unsigned emit_local_var()
{
    /* reg_pos must be 0 */
    emit_multi(8, "\x13\x01\xc1\xff\x23\x20\xa1\x00");
    /* FFC10113     add sp, sp, -4
       00A12023     sw a0, 0(sp) */
    stack_pos = stack_pos + 1;
    return 0 - stack_pos;
}

unsigned emit_global_var()
{
    global_pos = global_pos + 4;
    return global_pos - 2052;
}

unsigned emit_begin()
{
    stack_pos = 0;
    global_pos = 0;
    /* clang-format off */
    emit_multi(104, "\x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xf3\x00\x01\x00\x00\x00\x54\x00\x01\x00\x34\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x34\x00\x20\x00\x01\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x00........\x07\x00\x00\x00\x00\x10\x00\x00\x13\x00\x00\x00\x13\x00\x00\x00\x00\x00\x00\x00\x93\x68\xd0\x05\x73\x00\x00\x00");
    /* clang-format on */
    /*
    elf_header:
        0000 7f 45 4c 46    e_ident         0x7F, "ELF"
        0004 01 01 01 00                    1, 1, 1, 0
        0008 00 00 00 00                    0, 0, 0, 0
        000C 00 00 00 00                    0, 0, 0, 0
        0010 02 00          e_type          2 (executable)
        0012 03 00          e_machine       0xF3 (RISC-V)
        0014 01 00 00 00    e_version       1
        0018 00 00 01 00    e_entry         0x00010000 + _start
        001C 34 00 00 00    e_phoff         program_header_table
        0020 00 00 00 00    e_shoff         0
        0024 00 00 00 00    e_flags         0
        0028 34 00          e_ehsize        52 (program_header_table)
        002A 20 00          e_phentsize     32 (start - program_header_table)
        002C 01 00          e_phnum         1
        002E 00 00          e_shentsize     0
        0030 00 00          e_shnum         0
        0032 00 00          e_shstrndx      0

    program_header_table:
        0034 01 00 00 00    p_type          1 (load)
        0038 00 00 00 00    p_offset        0
        003C 00 80 04 08    p_vaddr         0x00010000 (default)
        0040 00 80 04 08    p_paddr         0x00010000 (default)
        0044 ?? ?? ?? ??    p_filesz
        0048 ?? ?? ?? ??    p_memsz
        004C 07 00 00 00    p_flags         7 (read, write, execute)
        0050 00 10 00 00    p_align         0x1000 (4 KiByte)

    _start:
        0054
        0058
        005C 00 00 00 00    jal x1, main
        0064 93 68 D0 05    or x17, x0, 93
        0068 73 00 00 00    ecall
    */

    return 92;
    /* return the address of the call to main() as a forward reference */
}

void emit_end()
{
    unsigned addr = code_pos + 1964; /* 2048 - 84 */
    set_32bit(buf + 84, 407 + (((addr + 2048) >> 12) << 12));
    /* 00000197  auipc gp, ADDR_HI */
    set_32bit(buf + 88, 98707 + (addr << 20));
    /* 00018193  addi gp, gp, ADDR_LO */
    unsigned i = 0;
    while (i < global_pos) {
        emit32(0);
        i = i + 4;
    }

    set_32bit(buf + 68, code_pos);
    set_32bit(buf + 72, code_pos);
}
