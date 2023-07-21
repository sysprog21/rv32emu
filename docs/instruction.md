# RISC-V instructions

In RISC-V, there are only a small number of instruction layouts (named with
letters: Register/register, Immediate/register, Store, Upper immediate, Branch,
and Jump), which is refreshing, and the choice to reserve two bits in the
fixed-width 32-bit format.

The S-type, B-type, and J-type instructions include immediate fields in a
slightly weird permutation. In response to the observation that sign-extension
was often a critical-path logic-design problem in modern CPUs, the designers
always put the immediate sign bit in the MSB of the instruction word, so you
can do sign-extension before instruction decoding is done, but this leads to
the J-type format
```
imm[20] || imm[10:1] || imm11 || imm[19:12] || rd || opcode
```
with a ±1MiB PC-relative range, and an only slightly-less-surprising B-type
format, with a ±4KiB range.

U-type instructions, of which there are only two (`lui` and `auipc`), have
a 20-bit immediate field, but I-type and S-type instructions (used for things
like `addi` and `slti` and, notably, memory loads and stores) have only a
12-bit immediate field.

## Instruction Format
```
31           25 24         20 19         15 14 12 11          7 6       0
+--------------+-------------+-------------+-----+-------------+---------+
|                   imm[31:12]                   |     rd      | 0110111 | LUI
|                   imm[31:12]                   |     rd      | 0010111 | AUIPC
|             imm[20|10:1|11|19:12]              |     rd      | 1101111 | JAL
|         imm[11:0]          |     rs1     | 000 |     rd      | 1100111 | JALR 
| imm[12|10:5] |     rs2     |     rs1     | 000 | imm[4:1|11] | 1100011 | BEQ
| imm[12|10:5] |     rs2     |     rs1     | 001 | imm[4:1|11] | 1100011 | BNE
| imm[12|10:5] |     rs2     |     rs1     | 100 | imm[4:1|11] | 1100011 | BLT
| imm[12|10:5] |     rs2     |     rs1     | 101 | imm[4:1|11] | 1100011 | BGE
| imm[12|10:5] |     rs2     |     rs1     | 110 | imm[4:1|11] | 1100011 | BLTU
| imm[12|10:5] |     rs2     |     rs1     | 111 | imm[4:1|11] | 1100011 | BGEU
|         imm[11:0]          |     rs1     | 000 |     rd      | 0000011 | LB
|         imm[11:0]          |     rs1     | 001 |     rd      | 0000011 | LH
|         imm[11:0]          |     rs1     | 010 |     rd      | 0000011 | LW
|         imm[11:0]          |     rs1     | 100 |     rd      | 0000011 | LBU
|         imm[11:0]          |     rs1     | 101 |     rd      | 0000011 | LHU
|  imm[11:5]   |     rs2     |     rs1     | 000 |  imm[4:0]   | 0100011 | SB
|  imm[11:5]   |     rs2     |     rs1     | 001 |  imm[4:0]   | 0100011 | SH
|  imm[11:5]   |     rs2     |     rs1     | 010 |  imm[4:0]   | 0100011 | SW
|         imm[11:0]          |     rs1     | 000 |     rd      | 0010011 | ADDI
|         imm[11:0]          |     rs1     | 010 |     rd      | 0010011 | SLTI
|         imm[11:0]          |     rs1     | 011 |     rd      | 0010011 | SLTIU
|         imm[11:0]          |     rs1     | 100 |     rd      | 0010011 | XORI
|         imm[11:0]          |     rs1     | 110 |     rd      | 0010011 | ORI
|         imm[11:0]          |     rs1     | 111 |     rd      | 0010011 | ANDI
|   0000000    |    shamt    |     rs1     | 001 |     rd      | 0010011 | SLLI
|   0000000    |    shamt    |     rs1     | 101 |     rd      | 0010011 | SRLI
|   0100000    |    shamt    |     rs1     | 101 |     rd      | 0010011 | SRAI
|   0000000    |     rs2     |     rs1     | 000 |     rd      | 0110011 | ADD
|   0100000    |     rs2     |     rs1     | 000 |     rd      | 0110011 | SUB
|   0000000    |     rs2     |     rs1     | 001 |     rd      | 0110011 | SLL
|   0000000    |     rs2     |     rs1     | 010 |     rd      | 0110011 | SLT
|   0000000    |     rs2     |     rs1     | 011 |     rd      | 0110011 | SLTU
|   0000000    |     rs2     |     rs1     | 100 |     rd      | 0110011 | XOR
|   0000000    |     rs2     |     rs1     | 101 |     rd      | 0110011 | SRL
|   0100000    |     rs2     |     rs1     | 101 |     rd      | 0110011 | SRA
|   0000000    |     rs2     |     rs1     | 110 |     rd      | 0110011 | OR
|   0000000    |     rs2     |     rs1     | 111 |     rd      | 0110011 | AND
```

## Pseudo-instructions

Pseudo-instructions give RISC-V a richer set of assembly language instructions.
The following example shows the `li` pseudo-instruction which is used to load immediate values:
```
.org 0
.globl _start
.text
_start:
    .equ CONSTANT, 0xcafebabe
    li a0, CONSTANT
```

which generates the following assembler output as seen by `objdump`:
```
00000000 <_start>:
   0:	cafec537        lui     a0,0xcafec
   4:	abe50513        addi    a0,a0,-1346 # cafebabe <CONSTANT+0x0>
```

The `lui` (load-upper-immediate) instruction has a 20-bit immediate, so the other immediate-load instructions have only 12-bit immediates.

## Instruction Examples

### Encoding instruction `0x01e007ef`

First, convert hex into binary:

| hex |  `0`   |  `1`   |   `e`  |   `0`  |   `0`  |   `7`  |   `e`  |   `f`  |
|:---:|:------:|:------:|:------:|:------:|:------:|:------:|:------:|:------:|
| bin | `0000` | `0001` | `1110` | `0000` | `0000` | `0111` | `1110` | `1111` |

Instructions are better decoded reading them from right to left, so that the first thing you find are its quadrant and opcode.

|  RV32I  | `00000001111000000000` | `01111`   |    `11011`    |    `11`      |
|:-------:|:----------------------:|:---------:|:-------------:|:------------:|
| Meaning |  `<imm> (encoded)`     |   `rd`    | `opcode`      | `quadrant`   |
|  Value  |  `<imm>`               |   15      | Jump and Link |  4th         |

We therefore obtain `jal x15 <imm>```, where `<imm>` is the still to be decoded immediate.

All the instruction fields except `<imm>` are decodable just from the tables, for this instruction. For `<imm>` we have to further scramble some bits to decode it.

Identify the subfields of the immediate:

| `<imm> (encoded)` |  `0` | `0000001111` | `0`  | `00000000` |
|:-----------------:|:----:|:------------:|:----:|:----------:|
|  Encoding         | `m2` |   `imm2`     | `m1` |  `imm1`    |

Re-order the subfields as written on the tables. Usually the immediate encoding is just below the corresponding instruction's encoding.

| Decoding di `<imm>` |    `-m2-`      |  `imm1`    | `m1` |   `imm2`     | `0` |
|:-------------------:|:--------------:|:----------:|:----:|:------------:|:---:|
| `<imm> (bin)`       | `000000000000` | `00000000` | `0`  | `0000001111` | `0` |

We obtain the 2's complement
- `<imm> (bin)` = `00000000000000000000000000011110`

that is the decimal
- `<imm> (dec)` = `30`

The complete disassembled instruction is therefore `jal x15 30`, or `jal a5 30` using the ABI register aliases.

### Deconding instruction `j -12`

The instruction `j`.

| Instruction Encoding `j` | `<imm> (encoded)` | 00000 | 11011 | 11 |
|:------------------------:|:-----------------:|:-----:|:-----:|:--:|

We therefore know the least significant bits `xxxxxxxxxxxxxxxxxxxx000001101111`. The rest is just an immediate, `<imm> = 30 `, which we now have to encode.
Then sign-extend it to cover all 32 bits of its width, converting `<imm> (bin)` = `11111111111111111110100` from `<imm> (bin)` = `-12` to `<imm> (bin)` = `10100`.

Divide this number into its subfields.

| `<imm> (bin)`    | `111111111111` | `11111111` | `1`  | `1111111010` | `0` |
|:----------------:|:--------------:|:----------:|:----:|:------------:|:---:|
| Decoding `<imm>` |    `-m2-`      |  `imm1`    | `m1` |   `imm2`     | `0` |

Re-order the fields to encode the immediate:

|       Encoding    | `m2` |   `imm2`     | `m1` |  `imm1`    |
|:-----------------:|:----:|:------------:|:----:|:----------:|
| `<imm> (encoded)` | `1`  | `1111111010` | `1`  | `11111111` |

We obtain `<imm> (encoded)` = `11111111010111111111xxxxxxxxxxxx`, that are the missing most significant bits.

The complete assembled instruction is therefore `11111111010111111111000001101111`, after having jointed the two half-results above.

## "RVC" compressed instructions

Chapter 12, p. 67 (79 of 145), explains a Thumb-2-like scheme, providing a
16-bit version of the instruction when:
* the immediate or address offset is small, or
* one of the registers is the zero register (`x0`), the ABI link register (`x1`),
  or the ABI stack pointer (x2), or
* the destination register and the first source register are identical, or
* the registers used are the 8 most popular ones.

It turns out, though, that the last two are actually an "and" rather than an
"or", and the conditions are actually considerably more restrictive than the
above implies.

There is an opcode map on pp. 81–83.

The designers point out that the Cray-1 also had 16-bit and 32-bit instruction
lengths, following Stretch, the 360, the CDC 6600, and followed by not only Arm
but also MIPS ("MIPS16" and "microMIPS") and PowerPC "VLE", and that RVC
"fetches 25%-30% fewer instruction bits, which reduces instruction cache misses
by 20%-25%, or roughly the same performance impact as doubling the instruction
cache size."

There are eight compressed instruction formats.
The eight registers accessible by the three-bit register fields in the `CIW` (immediate wide),
`CL` (load), `CS` (store), and `CB` (branch) formats are not the first eight registers,
but the second eight registers, `x8` – `x15`.
These are callee-saved `s0` – `s1` and the first argument registers `a0` – `a5`.
The `CR` (register–register), `CI` (immediate), and `CSS` (stack store) formats have
full-width five-bit register fields. (The `CJ` format does not refer to any registers.)

Complementing the stack-store format are stack-load instructions (p. 71) using
the CI format with a 6-bit immediate offset, which is prescaled by the data
size (4, 8, or 16 bits). These index only upward from the stack pointer,
institutionalizing the otherwise-only-conventional downward stack growth.
The immediate-offset field in the stack-store format is also 6 bits and treated
in the same way. And there is a thing called `c.addi16sp` which adds a signed
multiple of 16 to the stack pointer, i.e., allocates or deallocates stack space.

So in a 16-bit instruction you can load or store any of the 32 integer registers
to any of 64 stack slots (if you have allocated that many), and you can do a
two-operand operation with either two registers or a register and an immediate.
It is the more general load, store, and branch formats (`CL`, `CS`, `CB`) that
limit you to the 8 "popular" registers and only permit 5-bit unsigned offsets
(thus 32 slots indexed by those "popular" registers).

These general `CL` and `CS` formats effectively require the register to either be
used as a base pointer to a struct or contain a memory address computed in a
previous instruction, although you could reasonably argue that the 12-bit
immediate field in the uncompressed I-type and S-type instructions imposes a
similar restriction — 2 KiB is not very much space for all of your array base
addresses.

Additionally, on RV32C and RV64C, the CIW-format `c.addi4spn` loads a pointer
to any of 256 4-byte stack slots (specified in an immediate argument) into
one of the 8 popular registers, which you can then use with a `CL` or `CS`
instruction to access it.

Unconditional jumps and calls (to ±2KiB from PC) and branches on zeroness
(to ±256 bytes from PC) are also encodable in 16 bits, using the `CJ` format.
These are also restricted to the 8 popular registers. There are also `c.jr` and
`c.jalr` indirect unconditional jumps and calls, which can use any of the 32
registers except, of course, x0.

There are a couple of compressed load-immediate instructions with a 6-bit
immediate operand, of which the second (`c.lui`) seems entirely mysterious.

16-bit-encoded ALU instructions (subtract, `c.addw`, `c.subw`, copy, and, or,
xor, and shifts) are all limited to the 8 popular registers, except for addition,
which can use all 32 registers.

`ebreak` (into the debugger) is mapped into RVC, which is pretty important,
but `ecall` / `scall` is not.

There does not seem to be a reasonable way to load immediate memory addresses
in 16-bit code except through the deprecated c.jal .+2 approach, which leaves
the current PC in ra, at which point you can add a signed 6-bit immediate to
it with `c.addi`, thus generating an address of some constant (or maybe a
variable, if your page is mapped XWR or you do not have memory protection.)
within 32 bytes of where you are, but then it is still in `x1` and not a popular
register. There is no compressed version of the auipc instruction, for example.

In pure 16-bit instructions you can freely walk around pointer graphs, index
into arrays, jump around, jump up, jump up, and get down, add, subtract, and
do bitwise operations, but you can not invoke system calls or load addresses
of global variables or constants.

So you could almost do a 16-bit instruction RISC-V hardware core that emulates
other instructions with traps but executes at full speed when running 16-bit
instructions. You would need to add a few additional 16-bit instructions for
accessing CSRs, loading addresses, and handling traps.

## Decode RISC-V instructions

Various RISC-V instruction set simulators decode instructions using a series of nested `switch` statements.
First, they switch on non-C vs. C (compressed if implemented) bits 1:0.
Next, they switch on the "opcode" field bits 6:2, such as `OP-IMM`, `LOAD`, or `BRANCH`.
Then, they typically switch on the "funct3" field bits 14:12, which distinguish instructions like
`ADD`, `SLT`, `SLTU`, `AND`, `OR`, `XOR`, `SLL`, `SRL` for arithmetic operations,
or `BEQ`, `BNE`, `BLT`, `BLTU`, `BGE`, `BGEU` for conditional branches,
or the operand size for loads and stores.
Finally, for certain instructions, they switch on the "funct7" field bits 31:25 to differentiate between
`ADD`/`SUB` or `SRL`/`SRA`, for example.

## Reference
* [RISC-V: An Overview of the Instruction Set Architecture](http://web.cecs.pdx.edu/~harry/riscv/RISCV-Summary.pdf)
* [RISC-V Opcodes](https://github.com/riscv/riscv-opcodes)
* [RISC-V Instruction Set Metadata](https://github.com/michaeljclark/riscv-meta)
* [RISC-V Instruction-Set Cheatsheet](https://itnext.io/risc-v-instruction-set-cheatsheet-70961b4bbe8)
* [RISC-V Assembly Programmer's Manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/master/riscv-asm.md)
