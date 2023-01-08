/* code is data
 *
 * Compile this program with:
 *   riscv32-unknown-elf-gcc -O0 -march=rv32i -mabi=ilp32 -o jit jit.c
 *
 * NOTE: no optimization should be enabled.
 */

#include <stdint.h>
#include <stdio.h>

typedef int func_t(void);

/*
 * addi a0, zero, 56
 * Opcode addi: 0010011
 * destination register a0: 01010
 * funct3: 000
 * Source register zero: 00000
 * The immediate 56: 000000111000
 * 000000111000 | 00000 | 000 | 01010 | 0010011
 * All together: 00000011100000000000010100010011
 * (In hex: 0x3800513)
 */

int main(int argc, char *argv[])
{
    uint32_t instructions[] = {
        [0] = 0x03800513, /* addi a0, zero, 56 */
        [1] = 0x00008067, /* jalr zero, ra, 0 */
    };

    /* Reinterpret the array address as a function */
    func_t *jit = (func_t *) instructions;
    printf("%d\n", jit());

    return 0;
}
