.text

.globl get_cycles
.align 2
get_cycles:
    csrr a1, cycleh
    csrr a0, cycle
    csrr a2, cycleh
    bne a1, a2, get_cycles
    ret

.size get_cycles,.-get_cycles
