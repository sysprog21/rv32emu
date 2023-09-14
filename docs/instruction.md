# RISC-V instructions

## ISA extensions

RISC-V is designed for extensibility. A RISC-V platform must implement a base
integer instruction set (such as RV32I or RV64I, which are ratified, or RV32E
or RV128I, which are proposed) that defines a core set of basic instructions.
Additional functionality is then provided through optional extensions.
For example, the "M" extension adds integer multiplication and division
capabilities, while the "F" extension introduces support for single-precision
floating-point arithmetic.

## Instruction Encoding

In RISC-V, there are only a small number of instruction layouts (named with
letters: Register/register, Immediate/register, Store, Upper immediate, Branch,
and Jump), which is refreshing, and the choice to reserve two bits in the
fixed-width 32-bit format.

The S-type, B-type, and J-type instructions include immediate fields in a
slightly unusual permutation. In response to the observation that sign-extension
was often a critical-path logic-design problem in modern CPUs, the designers
consistently place the immediate sign bit in the MSB of the instruction word,
enabling sign-extension before instruction decoding is completed. However, this
approach results in the J-type format
```
imm[20] || imm[10:1] || imm11 || imm[19:12] || rd || opcode
```
with a ±1MiB PC-relative range, and an only slightly-less-surprising B-type
format, with a ±4KiB range.

U-type instructions, of which there are only two (`lui` and `auipc`), have
a 20-bit immediate field. However, I-type and S-type instructions, used for
operations like `addi`, `slti`, and notably memory loads and stores, have
only a 12-bit immediate field.

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

Pseudo-instructions provide RISC-V with a broader range of assembly language
instructions. The following example demonstrates the `li` pseudo-instruction,
which is used to load immediate values:
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

The `lui` (load-upper-immediate) instruction has a 20-bit immediate, while the
other immediate-load instructions only have 12-bit immediates.

## Instruction Examples

### Encoding instruction `0x01e007ef`

First, convert hex into binary:

| hex |  `0`   |  `1`   |   `e`  |   `0`  |   `0`  |   `7`  |   `e`  |   `f`  |
|:---:|:------:|:------:|:------:|:------:|:------:|:------:|:------:|:------:|
| bin | `0000` | `0001` | `1110` | `0000` | `0000` | `0111` | `1110` | `1111` |

Instructions are better decoded by reading them from right to left, where the
first encountered elements are their quadrant and opcode.

|  RV32I  | `00000001111000000000` | `01111`   |    `11011`    |    `11`      |
|:-------:|:----------------------:|:---------:|:-------------:|:------------:|
| Meaning |  `<imm> (encoded)`     |   `rd`    | `opcode`      | `quadrant`   |
|  Value  |  `<imm>`               |   15      | Jump and Link |  4th         |

Therefore, `jal x15 <imm>` is obtained, with `<imm>` representing the immediate
yet to be decoded.

All the instruction fields except `<imm>` can be decoded directly from the
tables for this instruction. For `<imm>`, further scrambling of some bits is
required to decode it.

Identify the subfields of the immediate:

| `<imm> (encoded)` |  `0` | `0000001111` | `0`  | `00000000` |
|:-----------------:|:----:|:------------:|:----:|:----------:|
|  Encoding         | `m2` |   `imm2`     | `m1` |  `imm1`    |

Re-order the subfields as written on the tables. Typically, the immediate
encoding is located just below the corresponding instruction's encoding.

| Decoding di `<imm>` |    `-m2-`      |  `imm1`    | `m1` |   `imm2`     | `0` |
|:-------------------:|:--------------:|:----------:|:----:|:------------:|:---:|
| `<imm> (bin)`       | `000000000000` | `00000000` | `0`  | `0000001111` | `0` |

We obtain the 2's complement
- `<imm> (bin)` = `00000000000000000000000000011110`

that is the decimal
- `<imm> (dec)` = `30`

The fully disassembled instruction is `jal x15, 30`, or `jal a5, 30` when using
the ABI register aliases.

### Decoding instruction `j -12`

The instruction `j`.

| Instruction Encoding `j` | `<imm> (encoded)` | 00000 | 11011 | 11 |
|:------------------------:|:-----------------:|:-----:|:-----:|:--:|

The least significant bits are `xxxxxxxxxxxxxxxxxxxx000001101111`.
The rest represents an immediate, `<imm> = 30`, which needs to be encoded.
Then sign-extend it to cover all 32 bits of its width, converting
`<imm> (bin) = 11111111111111111110100` from `<imm> (bin) = -12` to
`<imm> (bin) = 10100`.

Divide this number into its subfields.

| `<imm> (bin)`    | `111111111111` | `11111111` | `1`  | `1111111010` | `0` |
|:----------------:|:--------------:|:----------:|:----:|:------------:|:---:|
| Decoding `<imm>` |    `-m2-`      |  `imm1`    | `m1` |   `imm2`     | `0` |

Re-order the fields to encode the immediate:

|       Encoding    | `m2` |   `imm2`     | `m1` |  `imm1`    |
|:-----------------:|:----:|:------------:|:----:|:----------:|
| `<imm> (encoded)` | `1`  | `1111111010` | `1`  | `11111111` |

The obtained `<imm> (encoded) is 11111111010111111111xxxxxxxxxxxx`, representing
the missing most significant bits.

The complete assembled instruction is therefore
`11111111010111111111000001101111`, after combining the two half-results above.

## "RVC" compressed instructions

[Chapter 12](https://riscv.org/wp-content/uploads/2017/05/riscv-spec-v2.2.pdf),
page 67 (79 of 145) explains a Thumb-2-like scheme, providing a 16-bit version
of the instruction when:
* the immediate or address offset is small, or
* one of the registers is the zero register (`x0`), the ABI link register (`x1`),
  or the ABI stack pointer (`x2`), or
* both the destination register and the first source register are identical, or
* the registers used are the 8 most popular ones.

However, it turns out that the last two conditions are actually "and" rather
than "or," and the conditions are more restrictive than the above implies.

There is an opcode map on page 81–83.

The designers point out that the Cray-1 also had 16-bit and 32-bit instruction
lengths, following Stretch, the 360, the CDC 6600, and followed by not only
Arm but also MIPS ("MIPS16" and "microMIPS") and PowerPC "VLE." RVC fetches
25%-30% fewer instruction bits, resulting in a reduction of instruction cache
misses by 20%-25%, which is approximately equivalent to doubling the instruction
cache size in terms of performance impact.

There are 8 compressed instruction formats.
The eight registers accessible by the three-bit register fields in the `CIW` (immediate wide),
`CL` (load), `CS` (store), and `CB` (branch) formats are not the first eight registers,
but the second eight registers, `x8` – `x15`.
These are callee-saved `s0` – `s1` and the first argument registers `a0` – `a5`.
The `CR` (register–register), `CI` (immediate), and `CSS` (stack store) formats have
full-width five-bit register fields. (The `CJ` format does not refer to any registers.)

Complementing the stack-store format are stack-load instructions (page 71)
using the `CI` format with a 6-bit immediate offset, prescaled by the data
size (4, 8, or 16 bits). These instructions only index upward from the stack
pointer, institutionalizing the otherwise conventional downward stack growth.
The immediate-offset field in the stack-store format is also 6 bits and treated
in the same way. Additionally, there is a instruction called `c.addi16sp`, which
adds a signed multiple of 16 to the stack pointer, effectively allocating or
deallocating stack space.

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

In pure 16-bit instructions, one can freely navigate pointer graphs, index into
arrays, perform jumps, addition, subtraction, and bitwise operations. However,
invoking system calls or loading addresses of global variables or constants is
not possible.

With this in mind, an almost complete 16-bit instruction RISC-V hardware core
could be designed to emulate other instructions with traps, while running 16-bit
instructions at full speed. A few additional 16-bit instructions would be
required to handle accessing CSRs, loading addresses, and managing traps.

## Decode RISC-V instructions

Various RISC-V instruction set simulators decode instructions using a series of
nested `switch` statements.

First, they switch on non-C vs. C (compressed if implemented) bits 1:0.
Next, they switch on the "opcode" field bits 6:2, such as `op-imm`, `load`, or
`branch`. Then, they typically switch on the "funct3" field bits 14:12, which
distinguish instructions like `add`, `slt`, `sltu`, `and`, `or`, `xor`, `sll`,
`srl` for arithmetic operations, or `beq`, `bne`, `blt`, `bltu`, `bge`, `bgeu`
for conditional branches, or the operand size for loads and stores.
Finally, for certain instructions, they switch on the "funct7" field bits 31:25
to differentiate between `add`/`sub` or `srl`/`sra`, for example.

## Reference
* [RISC-V: An Overview of the Instruction Set Architecture](http://web.cecs.pdx.edu/~harry/riscv/RISCV-Summary.pdf)
* [RISC-V Opcodes](https://github.com/riscv/riscv-opcodes)
* [RISC-V Instruction Set Metadata](https://github.com/michaeljclark/riscv-meta)
* [RISC-V Instruction-Set Cheatsheet](https://itnext.io/risc-v-instruction-set-cheatsheet-70961b4bbe8)
* [RISC-V Assembly Programmer's Manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/master/riscv-asm.md)
