/* Standard Library Functions */

/* clang-format off */

void exit(int) _Pragma("emit \x93\x08\xd0\x05\x73\x00\x00\x00");
    /*  li a7, 93       # sys_exit
        ecall */

int getchar(void) _Pragma("emit \x13\x01\xc1\xff\x13\x05\x00\x00\x93\x05\x01\x00\x13\x06\x10\x00\x93\x08\xf0\x03\x73\x00\x00\x00\x93\x05\x05\x00\x03\x45\x01\x00\x13\x01\x41\x00\x63\x44\xb0\x00\x13\x05\xf0\xff\x67\x80\x00\x00");
    /*  add sp, sp, -4
        li a0, 0        # stdin
        mv a1, sp       # buffer on stack
        li a2, 1        # 1 byte
        li a7, 63       # sys_read
        ecall
        mv a1, a0       # return a0==0 ? -1 : (sp)
        lw a0, 0(sp)
        add sp, sp, 4
        bgtz a1, 1f
        li a0, -1
     1: ret */

void *malloc(unsigned long) _Pragma("emit \x13\x01\x41\xff\x23\x24\xa1\x00\x13\x05\x00\x00\x93\x08\x60\x0d\x73\x00\x00\x00\x23\x20\xa1\x00\x83\x28\x81\x00\x33\x05\x15\x01\x23\x22\xa1\x00\x93\x08\x60\x0d\x73\x00\x00\x00\x83\x28\x41\x00\x63\x08\x15\x01\x13\x05\x00\x00\x13\x01\xc1\x00\x67\x80\x00\x00\x03\x25\x01\x00\x13\x01\xc1\x00\x67\x80\x00\x00");
    /*  add sp, sp, -12
        sw a0, 8(sp)    # size
        li a0, 0
        li a7, 214      # sys_brk
        ecall
        sw a0, 0(sp)    # end
        lw a7, 8(sp)    # size
        add a0, a0, a7
        sw a0, 4(sp)    # end + size
        li a7, 214      # sys_brk
        ecall
        lw a7, 4(sp)    # end + size
        beq a0, a7, 1f
        li a0, 0
        add sp, sp, 12
        ret
     1: lw a0, 0(sp)    # old end
        add sp, sp, 12
        ret */

int write(int, char *, int) _Pragma("emit \x93\x08\x00\x04\x73\x00\x00\x00\x67\x80\x00\x00");
    /*  li a7, 64       # sys_write
        ecall
        ret */

int read(int, char *, int) _Pragma("emit \x93\x08\xf0\x03\x73\x00\x00\x00\x67\x80\x00\x00");
    /*  li a7, 63       # sys_read
        ecall
        ret */

int putchar(int) _Pragma("emit \x13\x05\x10\x00\x93\x05\x01\x00\x13\x06\x10\x00\x93\x08\x00\x04\x73\x00\x00\x00\x67\x80\x00\x00");
    /*  li a0, 1
        mv a1, sp
        li a2, 1
        li a7, 64       # sys_write
        ecall
        ret */

/* clang-format on */
