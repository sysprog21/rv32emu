# System Calls

## Background

System calls serve as the principal method of interaction between an application and the underlying operating system kernel.
They enable applications to access services and resources provided by the OS kernel,
such as file operations, network communication, and memory management.
System calls are essential for performing privileged operations that applications can not directly execute,
ensuring a secure and controlled environment.
To directly interact with system calls without relying on external libraries, programmers can invoke them from assembler programs.
For a comprehensive list of available system calls and detailed information about their usage, see [`man 2 syscalls`](http://man7.org/linux/man-pages/man2/syscalls.2.html).

## System Calls in C

In C, a list of parameters is passed to the kernel in a certain sequence.
For example, for the `write` system call, the parameters are structured as follows (see [`man 2 write`](https://man7.org/linux/man-pages/man2/write.2.html)):
```c
ssize_t write(int fd, const void *buf, size_t count);
```

The three parameters passed are a file descriptor, a pointer to a character buffer (essentially a string),
and the number of characters in that string to be written.
The file descriptor can represent the standard output.
It is important to note that the string is not zero-terminated.

## RISC-V calling conventions

The RISC-V convention, as defined by the [Calling Convention](https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf),
involves mapping these parameters one by one to the registers, starting at `a0`.
The system call number is placed in `a7`, and then the `ecall` instruction is executed.

Here is how the parameters are mapped:
- The file descriptor number is placed in `a0`.
- The pointer to the string (its address) is placed in `a1`.
- The number of characters to print is placed in `a2`.

For determining the specific system call number for the 'write' operation in the context of `rv32emu`,
reference can be made to [riscv-pk/pk/syscall.h](https://github.com/riscv/riscv-pk/blob/master/pk/syscall.h).
In the case of the `write` system call, the associated number is `64`, which is then assigned to the register `a7`.

Following a successful write operation, the count of written characters should be present in the `a0` register.
Conversely, if an error occurs, the return value is represented as `-1`.

For instance, to print the string "RISC-V" and subsequently exit using the exit system call,
the GNU GCC compiler suite in combination with `rv32emu` offers a feasible approach.
The following is a potential program in assembly code (`hello.S`):
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

The program is assembled, linked, and executed on `rv32emu` using the following commands:
```shell
$ riscv-none-elf-gcc -march=rv32i -mabi=ilp32 -nostartfiles -nostdlib -o hello hello.S
$ build/rv32emu hello
```

An `ecall` instruction is used to trigger a trap into the kernel,
and there exist three privilege modes: User mode, Supervisor mode, and Machine mode.
Corresponding to these modes, there are three versions of the iret instruction:
`uret` which can be used from any mode if the `N-extension` is enabled to allow user-mode trap handlers,
`sret` which is used from S-mode (or M-mode), and `mret` which can only be used from M-mode.

NOTE: On RV32 and RV64 architectures, the stack pointer must always be aligned to a 16-byte boundary.

## Newlib integration

The [newlib](https://sourceware.org/newlib/) library encompasses most of the C standard library functionality,
excluding thread support.
C functions like `malloc`, `printf`, `memcpy`, and many others are included in its implementation.
To enable cross-compiled binary files to run on this emulator,
`rv32emu` offers a fundamental subset of necessary system calls that [newlib](https://sourceware.org/newlib/) requires.

A subsystem is also provided to support a limited set of POSIX-compliant system calls.
Presently, only a few system calls are accessible through semihosting.
This mechanism allows code executed on a RISC-V target to interact with and utilize the I/O capabilities of the host computer.

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

## Experimental Display, Event, and Sound System Calls

These system calls are solely for the convenience of accessing the [SDL library](https://www.libsdl.org/) and [SDL2_Mixer](https://wiki.libsdl.org/SDL2_mixer) and are only intended for the presentation of RISC-V graphics applications. They are not present in the ABI interface of POSIX or Linux.

### `draw_frame` - Draw a frame around the SDL window

**system call number**: `0xBEEF`

**synopsis**: `void draw_frame(void *base, int width, int height)`

If a window does not already exist, one will be created with the specified `width` and `height`. The buffer pointed to by `base` will replace the content of the framebuffer, passing a different `width` or `height` compared to the size of the window is undefined behavior. This system call additionally polls events from the SDL library, and, if necessary, update the internal input specific event queue.

The width and height are merely the virtual dimensions of the screen; they are unrelated to the window's real size. The system call would deal with resizing events internally when they occurred.

### `setup_queue` - Setup input system's dedicated event and submission queue

**system call number**: `0xC0DE`

**synopsis**: `void setup_queue(void *base, size_t capacity, size_t *event_count)`

The user must pass a continuous memory chunk that contains two tightly packed queues, the event queue and the submission queue. And the submission queue is immediately following the last element of the event queue, which is the event queue's base address plus the size of each event element multiplied by the given capacity. If the capacity is not a power of two, it will be treated as the rounded value of the next highest power of two. Additionally, because the event counter variable serves as a notifier to the user that an event has been added to the event queue, it is critical to initialize it before passing its address to this system call.

#### Events

An event entry is made up of a 32-bit value representing the event's type and a `union` buffer containing the event's parameters.

* `KEY_EVENT`: Either a key is pressed or released. Its value buffer is made up of a 32-bit universal key code and an 8-bit state flag; if the corresponding character of the pressed key is not printable, the bit right after the most significant bit is set; for example, the "a" key's corresponding character is printable, so its keycode is the ASCII code of the "a" character, which is `0x61`. However, because the left shift key doesn't have a corresponding printable key, its hexadecimal value is `0x400000E1`, with the 31 bit is set.
* `MOUSE_MOTION_EVENT`: The cursor is moved during the current frame. This event contains two signed integer value, which is the delta of x position and y position respectively. If the relative mouse mode is enabled, the mouse movement will never be 0 because the cursor is wrapped within the canvas and is repeated whenever the cursor reaches the border.
* `MOUSE_BUTTON_EVENT`: The state of a mouse button has been changed. Its value buffer contains a 8-bit button value(1 is left, 2 is middle, 3 is right and so on) and an 8-bit boolean flag that indicates whether the mouse button is pressed.

### `submit_queue` - Notify the emulator a submission has been pushed into the submission queue

**system call number**: `0xFEED`

**synopsis**: `void submit_queue(size_t count)`

To inform the emulator that a batch of submissions should be processed, the application code should push several submissions into the queue first, and then pass the size of the submissions batch to this system call; the submissions will be processed and executed sequentially and immediately.

#### Submissions

The submission entry is structured similarly to an event entry, with a 32-bit type field and an associated dynamic-sized value buffer whose width depends on the type of submission.

* `RELATIVE_MODE_SUBMISSION`: Enable or disable the mouse relative mode. If the mouse relative mode is enabled, the mouse cursor is wrapped within the window border, it's associated with an 8-bit wide boolean value that indicates whether the relative mouse mode should be enbled.

### `control_audio` - control the behavior of music and sound effect(sfx)

**system call number**: `0xD00D`

**synopsis**: `void control_audio(int request)`

The application must prepare the sound data and then give the address of the sound data to register `a1`, the volume of the music to register `a2` (if necessary), and looping to register `a3` (if necessary).  in order to ask the emulator to perform some sound operations. The request will be processed as soon as possible. Three different sorts of requests exist:
* `PLAY_MUSIC`: Play the music. If any music is currently playing, it will be changed to something new. In order to avoid blocking on the main thread, the music is played by a new thread.
* `STOP_MUSIC`: Halt the music.
* `SET_MUSIC_VOLUME`: If necessary, adjust the music level at some point.

The request is similar to how music is managed earlier in the description and supports one type of sound effect request, however it does not support looping:
* `PLAY_SFX`: Play the sound effect, however keep in mind that playing too many at once may cause the channel to run out and the sound effects to stop working. A new thread is used to play the sound effect, and it does so for the same reason as `PLAY_MUSIC`.

#### Music
Music data is defined in a structure called `musicinfo_t`. The SDL2_mixer library is used by the emulator to play music using the fields `data` and `size` in the structure.

#### Sound Effect(sfx)
`sfxinfo_t` is a structure that defines sound effect data and size. The `data` and `size` fields of the structure are used to play sound effect with the SDL2_mixer library. Currently, support sound effect of [Doom's WAV](https://doomwiki.org/wiki/Sound) format and [normal WAV](https://en.wikipedia.org/wiki/WAV) format(includes [RIFF header](https://en.wikipedia.org/wiki/Resource_Interchange_File_Format)).

### `setup_audio` - setup or shutdown sound system

**system call number**: `0xBABE`

**synopsis**: `void setup_audio(int request)`

The majority of games, such as Doom and Quake, will maintain its own sound system or server, hence there should be a pair of initialization and destruct semantics. I believe that we should give users access to activities like startup and termination in order to maintain the semantic clarity. If not, the emulator will repeatedly check whether audio is enabled before playing music or sound effects.

Requesting the sound system to be turned on or off in the emulator. When the audio device is not in the busy status, the request will be handled. Two categories of requests exist:
* `INIT_AUDIO`: Setup the audio device and the sfx samples buffer. After initialization, open the audio device and get ready to play some music.
* `SHUTDOWN_AUDIO`: Release all the resources that initialized by `INIT_SOUND` request.
