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

### `poll_event(event)` - Poll a input specific event from SDL

**system call number**: `0xC0DE`

**synopsis**: `int poll_event(void *event)`

`event` will be filled with the polled event data, it should have a 32-bit `type` field, and associated with an appropriately sized value buffer. The internal event queue will be updated everytime `draw_frame` is called.

Currently accepted event types include the following:
* `KEY_EVENT` - `0x0`: Triggered when the status of keyboard changes, either when a key is pressed or released, and it returns a 32-bit keycode and a 8-bit key state, the values of the hexadecimal keycodes are listed in [SDL Keycode Lookup Table](https://wiki.libsdl.org/SDLKeycodeLookup)
* `MOUSE_MOTION_EVENT` - `0x1`: A mouse move event, with relative position information, two 32-bit signed integers that correspond to the x and y delta value, the mouse is continually wrapped in the window border by default.
* `MOUSE_BUTTON_EVENT` - `0x2`: the user code receives this event whenever the state of a mouse button changes, whether a button is pressed or released, and it returns a 8-bit value that indicates which button is updated(1 is left, 2 is middle, 3 is right and so on), as well as a 8-bit state value that indicates whether the button is pressed.
