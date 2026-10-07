/* Short loops must compile through cycle detection, below THRESHOLD. */
    .text
    .global _start, self_loop, cycle_head, linear
_start:
    li t0, 100
    li t1, 0
    j self_loop
self_loop:
    addi t1, t1, 1
    blt t1, t0, self_loop
    li t1, 0
    j cycle_head
cycle_head:
    addi t1, t1, 1
    j cycle_tail
cycle_tail:
    blt t1, t0, cycle_head
    j linear
linear:
    li a0, 0
    li a7, 93
    ecall
