CAUSE_MISALIGNED_INSN_FETCH = 0x0
CAUSE_MISALIGNED_LOAD  = 0x4
CAUSE_MISALIGNED_STORE = 0x6

.section .misdata, "aw", @progbits
.global misalign_data
.type misalign_data, @object
misalign_data:
    .word = 0xBAAAAAAD
    . = 0x8000003  # force misaligned address

.section .mistext, "ax", @progbits
.global misalign_func
.type misalign_func, @function
misalign_func:
    . = 0x10000007 # force misaligned address
    addi t0, t0, 1

.section .text
.global _start
_start:
    call main
    j exit

# Assume three traps are delegated to supervisor,
# so handle them using supervisor CSR
.global misalign_trap_handler
misalign_trap_handler:
    csrr t0, scause

    # Check for misaligned instruction fetch
    li t1, CAUSE_MISALIGNED_INSN_FETCH
    beq t0, t1, misaligned_insn_fetch_handler

    # Check for misaligned load
    li t1, CAUSE_MISALIGNED_LOAD
    beq t0, t1, misaligned_load_handler

    # Check for misaligned store
    li t1, CAUSE_MISALIGNED_STORE
    beq t0, t1, misaligned_store_handler

    # If none of the above, exit failed
fail:
    li a0, 1
    j exit

exit:
    li a7, 93
    ecall

misaligned_insn_fetch_handler:
    # The trapping jump does not write its link register, so ra is not the
    # return address. Resume after the 4-byte jump (this test runs without C)
    # and use sret, which exits trap mode and clears is_trapped
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret

misaligned_load_handler:
    # Handle load misalignment: skip load
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret

misaligned_store_handler:
    # Handle store misalignment: skip store
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret
