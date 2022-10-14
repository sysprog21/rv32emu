# System Calls

## System Call Calling Protocol

RISC-V
```
ecall	a7	a0	a1
```

meaning that the syscall number is in register x17 (i.e., a7) and, optionally,
the first and second parameters to the syscall are in registers x10 (i.e., a0)
and x11 (i.e., a1), respectively.

## Newlib integration

The [newlib](https://sourceware.org/newlib/) implements a whole C standard library, minus threads. C functions such as `malloc`, `printf`, `memcpy`, and many more are implemented. `rv32emu` provides the essential subset of system calls required by [newlib](https://sourceware.org/newlib/), so that it allows cross-compiled binary files running on `rv32emu`.

## Experimental Display and Event System Calls

These system calls are solely for the convenience of accessing the [SDL library](https://www.libsdl.org/) and are only intended for the presentation of RISC-V graphics applications. They are not present in the ABI interface of POSIX or Linux.

### `draw_frame` - Draw a frame around the SDL window

**system call number**: `0xBEEF`

**synopsis**: `void draw_frame(void *screen, int width, int height)`

If a window does not already exist, one will be created with the specified `width` and `height`. The `screen` buffer will replace the content of the framebuffer, passing a different `width` or `height` compared to the size of the window is undefined behavior. This system call additionally polls events from the SDL library, and, if necessary, update the internal input specific event queue.

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
