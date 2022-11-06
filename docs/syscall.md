# System Calls

## Background

The primary means of communication between an application and the operating system kernel are system calls.
To avoid using other libraries, you can access them from assembler programs.
See [`man 2 syscalls`](http://man7.org/linux/man-pages/man2/syscalls.2.html) for more information and a complete list of system calls.

## System Calls in C

In C, a list of parameters is passed to the kernel in a certain sequence. e.g., for `write`, we have (see [`man 2 write`](https://man7.org/linux/man-pages/man2/write.2.html)):
```c
ssize_t write(int fd, const void *buf, size_t count);
```

The three parameters passed are a file descriptor, a pointer to a character buffer (in other words, a string) and the number of characters in that string to be printed.
The file descriptor can be the Standard Output.
Note the string is not zero-terminated.

## RISC-V calling conventions

The RISC-V convention as defined by [Calling Convention](https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf) is to map these parameters one by one to the registers starting at `a0`, put the number for the system call in `a7`, and execute the `ecall` instruction.
* The file descriptor number goes in `a0`.
* The pointer to the string -- its address -- goes in `a1`.
* The number of characters to print goes in `a2`.

To find the magic number for the system call for write and the `rv32emu`, we take a look at [riscv-pk/pk/syscall.h](https://github.com/riscv/riscv-pk/blob/master/pk/syscall.h).
It turns out to be `64`. This goes into `a7`.

After a successful write, we should receive the number of characters written in `a0`.
If there was an error, the return value should be `-1`.

To print the string "RISC-V" and then quit with the exit system call with the GNU GCC compiler suite and the `rv32emu`, this is one possible solution: (`hello.S`)

```assembly
    .equ STDOUT, 1
    .equ WRITE, 64
    .equ EXIT, 93

    .section .rodata
    .align 2
msg:
    .ascii "RISC-V\n"

    .section .text
    .align 2

    .globl  _start
_start:
    li a0, STDOUT  # file descriptor
    la a1, msg     # address of string
    li a2, 7       # length of string
    li a7, WRITE   # syscall number for write
    ecall

    # MISSING: Check for error condition
    li a0, 0       # 0 signals success
    li a7, EXIT
    ecall
```

We assemble, link and run this program for `rv32emu` with
```shell
riscv-none-elf-gcc -march=rv32i -mabi=ilp32 -nostartfiles -nostdlib -o hello hello.S
build/rv32emu hello
```

## Newlib integration

The [newlib](https://sourceware.org/newlib/) implements a whole C standard library, minus threads. C functions such as `malloc`, `printf`, `memcpy`, and many more are implemented. `rv32emu` provides the essential subset of system calls required by [newlib](https://sourceware.org/newlib/), so that it allows cross-compiled binary files running on `rv32emu`.

There is one subsystem supported which supplies a very limited set of POSIX-compliant system calls.
Currently only a couple system calls are supported via semihosting, which enables code running on RISC-V target to communicate with and use the I/O of the host computer.

|#     | System call     | Current support |
|------|-----------------|-----------------|
|   57 | `close`         | Deletes a descriptor from the per-process object reference table. |
|   62 | `lseek`         | Repositions the file offset of the open file description to the argument offset according to the directive whence. |
|   63 | `read`          | Reads specific bytes of data from the object referenced by the descriptor fildes into the buffer. |
|   64 | `write`         | Prints the buffer as a string to the specified file descriptor. |
|   80 | `fstat`         | No effect. |
|   93 | `exit`          | Terminates with a status code. |
|  169 | `gettimeofday`  | Gets date and time. Current time zone is NOT obtained. |
|  214 | `brk`           | Supports updating the program break and returning the current program break. |
|  403 | `clock_gettime` | Retrieves the value used by a clock which is specified by clock-id. |
| 1024 | `open`          | Opens or creates a file for reading or writing. |

Any other system calls will fail with an "unknown syscall" error.

## Experimental Display and Event System Calls

These system calls are solely for the convenience of accessing the [SDL library](https://www.libsdl.org/) and are only intended for the presentation of RISC-V graphics applications. They are not present in the ABI interface of POSIX or Linux.

### `draw_frame` - Draw a frame around the SDL window

**system call number**: `0xBEEF`

**synopsis**: `void draw_frame(void *screen, int width, int height)`

If a window does not already exist, one will be created with the specified `width` and `height`. The `screen` buffer will replace the content of the framebuffer, passing a different `width` or `height` compared to the size of the window is undefined behavior. This system call additionally polls events from the SDL library, and, if necessary, update the internal input specific event queue.

The width and height are merely the virutal dimensions of the screen; they are unrelated to the window's real size. The system call would deal with resizing events internally when they occurred.

### `setup_queue` - Setup input system's dedicated event and submission queue

**system call number**: `0xC0DE`

**synopsis**: `void *setup_queue(void *base, int capacity, unsigned int *event_count)`

The user must pass a continuous memory chunk that contains two tightly packed queues, the event queue and the submission queue. And the submission queue is immediately following the last element of the event queue, which is the event queue's base address plus the size of each event element multiplied by the given capacity. If the capacity is not a power of two, it will be treated as the rounded value of the next highest power of two. Additionally, because the event counter variable serves as a notifier to the user that an event has been added to the event queue, it is critical to initialize it before passing its address to this system call.

#### Events

An event entry is made up of a 32-bit value representing the event's type and a `union` buffer containing t1he event's parameters.

* `KEY_EVENT`: Either a key is pressed or released. Its value buffer is made up of a 32-bit universal key code and an 8-bit state flag; if the corresponding character of the pressed key is not printable, the bit right after the most significant bit is set; for example, the "a" key's correspoding character is printable, so its keycode is the ASCII code of the "a" character, which is `0x61`. However, because the left shift key doesn't have a corresponding printable key, its hexadecimal value is `0x400000E1`, with the 31 bit is set.
* `MOUSE_MOTION_EVENT`: The cursor is moved during the current frame. This event contains two signed integer value, which is the delta of x position and y position respectively. If the relative mouse mode is enabled, the mouse movement will never be 0 because the cursor is wrapped within the canvas and is repeated whenever the cursor reaches the border. 
* `MOUSE_BUTTON_EVENT`: The state of a mouse button has been changed. Its value buffer contains a 8-bit button value(1 is left, 2 is middle, 3 is right and so on) and an 8-bit boolean flag that indicates whether the mouse button is pressed.

### `submit_queue` - Notify the emulator a submission has been pushed into the submission queue

**system call number**: `0xFEED`

**synopsis**: `void submit_queue(int count)`

To inform the emulator that a batch of submissions should be processed, the application code should push several submissions into the queue first, and then pass the size of the submissions batch to this system call; the submissions will be processed and executed sequentially and immediately.

#### Submissions

The submission entry is structured similarly to an event entry, with a 32-bit type field and an associated dynamic-sized value buffer whose width depends on the type of submission.

* `RELATIVE_MODE_SUBMISSION`: Enable or disable the mouse relative mode. If the mouse relative mode is enabled, the mouse cursor is wrapped within the window border, it's associated with an 8-bit wide boolean value that indicates whether the relative mouse mode should be enbled.
