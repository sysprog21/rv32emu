.section .text.setup
TRAPFRAME_SIZE = 35 * 4
PG_SIZE = 4096
/* Separate stack areas for user and kernel/trap handler to avoid corruption.
 * Kernel stack: _end + PG_SIZE (for trap handler/trapframe)
 * User stack: _end + 3*PG_SIZE (2 pages above kernel stack, grows down)
 *
 * Layout: [kernel BSS][gap][trapframe|kernel stack][gap][user stack grows down]
 *                     _end  _end+PG_SIZE          _end+2*PG_SIZE  _end+3*PG_SIZE
 *
 * Note: User stack uses physical address for syscall compatibility.
 * The emulator's syscall handlers (ELF_LOADER mode) access guest memory
 * via physical addresses, not virtual addresses.
 */
KERNEL_STACK_TOP = _end + PG_SIZE
USER_STACK_TOP = _end + 3 * PG_SIZE

# FIXME: implement proper machine trap vector
# Since I assume that all interrupts and exceptions are
# handled by S-mode software, so the machine trap
# vector is not that much important in here
machine_trap_vector:
    j _exit;

.globl _start
_start:
    # init regs
    li x1, 0
    li x2, 0
    li x3, 0
    li x4, 0
    li x5, 0
    li x6, 0
    li x7, 0
    li x8, 0
    li x9, 0
    li x10, 0
    li x11, 0
    li x12, 0
    li x13, 0
    li x14, 0
    li x15, 0
    li x16, 0
    li x17, 0
    li x18, 0
    li x19, 0
    li x20, 0
    li x21, 0
    li x22, 0
    li x23, 0
    li x24, 0
    li x25, 0
    li x26, 0
    li x27, 0
    li x28, 0
    li x29, 0
    li x30, 0
    li x31, 0

    la sp, KERNEL_STACK_TOP - TRAPFRAME_SIZE
    csrw mscratch, sp;

    #la t0, machine_trap_vector
    #csrw mtvec, t0
    #csrr t0, mtvec # for debugging

    # init virtual memory
    j vm_boot

.globl _exit
_exit:
    li a7, 93
    ecall

.globl user_entry
user_entry:
    la sp, USER_STACK_TOP  # Use separate user stack
    jr x0  # jump to user space main at VA 0x0

.globl supervisor_trap_entry
supervisor_trap_entry:
    # get trapframe pointer (save a0 into scratch)
    csrrw a0, sscratch, a0;

    # push regs into trapframe
    sw x1, 0*4(a0);
    sw x2, 1*4(a0);
    sw x3, 2*4(a0);
    sw x4, 3*4(a0);
    sw x5, 4*4(a0);
    sw x6, 5*4(a0);
    sw x7, 6*4(a0);
    sw x8, 7*4(a0);
    sw x9, 8*4(a0);
    sw x11, 10*4(a0);
    sw x12, 11*4(a0);
    sw x13, 12*4(a0);
    sw x14, 13*4(a0);
    sw x15, 14*4(a0);
    sw x16, 15*4(a0);
    sw x17, 16*4(a0);
    sw x18, 17*4(a0);
    sw x19, 18*4(a0);
    sw x20, 19*4(a0);
    sw x21, 20*4(a0);
    sw x22, 21*4(a0);
    sw x23, 22*4(a0);
    sw x24, 23*4(a0);
    sw x25, 24*4(a0);
    sw x26, 25*4(a0);
    sw x27, 26*4(a0);
    sw x28, 27*4(a0);
    sw x29, 28*4(a0);
    sw x30, 29*4(a0);
    sw x31, 30*4(a0);

    # load stack pointer and save trapframe pointer into scratch
    csrrw t0, sscratch, a0;
    sw t0, 9*4(a0);

    # push status, epc, badaddr, cause
    csrr t0, sstatus;
    sw t0, 31*4(a0);
    csrr t0, sepc;
    sw t0, 32*4(a0);
    csrr t0, stval;
    sw t0, 33*4(a0);
    csrr t0, scause;
    sw t0, 34*4(a0);

    csrr sp, sscratch;
    j handle_trap;

.globl pop_tf
pop_tf:
    # a0 need to save trapframe pointer
    # pop epc and regs from trapframe
    lw t0, 32*4(a0)
    csrw sepc, t0
    lw x1, 0*4(a0)
    lw x2, 1*4(a0)
    lw x3, 2*4(a0)
    lw x4, 3*4(a0)
    lw x5, 4*4(a0)
    lw x6, 5*4(a0)
    lw x7, 6*4(a0)
    lw x8, 7*4(a0)
    lw x9, 8*4(a0)
    lw x11, 10*4(a0)
    lw x12, 11*4(a0)
    lw x13, 12*4(a0)
    lw x14, 13*4(a0)
    lw x15, 14*4(a0)
    lw x16, 15*4(a0)
    lw x17, 16*4(a0)
    lw x18, 17*4(a0)
    lw x19, 18*4(a0)
    lw x20, 19*4(a0)
    lw x21, 20*4(a0)
    lw x22, 21*4(a0)
    lw x23, 22*4(a0)
    lw x24, 23*4(a0)
    lw x25, 24*4(a0)
    lw x26, 25*4(a0)
    lw x27, 26*4(a0)
    lw x28, 27*4(a0)
    lw x29, 28*4(a0)
    lw x30, 29*4(a0)
    lw x31, 30*4(a0)

    # Properly restore a0: load saved a0 into sscratch before swap
    lw t0, 9*4(a0)       # Load saved a0 from trapframe
    csrw sscratch, t0    # Put saved a0 into sscratch
    lw t0, 4*4(a0)       # Reload t0 (corrupted by above)

    # swap trapframe pointer with restored a0
    csrrw a0, sscratch, a0

    sret
