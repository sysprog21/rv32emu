/* Fenster - the most minimal cross-platform GUI library
 *
 * Copyright (c) 2022 Serge Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Source: https://github.com/zserge/fenster
 *
 * Modified by Alan Jian <alanjian85@outlook.com>
 *   - Port Fenster to rv32emu.
 *   - Use the unique SDL-oriented system calls of rv32emu and clock_gettime()
 */

#ifndef FENSTER_H
#define FENSTER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct fenster {
    const char *title;
    const int width, height;
    uint32_t *buf;
    int keys[256]; /* keys are mostly ASCII, but arrows are 17..20 */
    int mod;       /* mod is 4 bits mask, ctrl=1, shift=2, alt=4, meta=8 */
    int x, y;
    int mouse;
    void *event_queue;
    uint32_t event_count;
    size_t event_queue_start;
};

#ifndef FENSTER_API
#define FENSTER_API extern
#endif
FENSTER_API int fenster_open(struct fenster *f);
FENSTER_API int fenster_loop(struct fenster *f);
FENSTER_API void fenster_close(struct fenster *f);
FENSTER_API void fenster_sleep(int64_t ms);
FENSTER_API int64_t fenster_time(void);
#define fenster_pixel(f, x, y) ((f)->buf[((y) * (f)->width) + (x)])

#ifndef FENSTER_HEADER
#define RV_QUEUE_CAPACITY 128

enum {
    RV_KEYCODE_RETURN = 0x0000000D,
    RV_KEYCODE_UP = 0x40000052,
    RV_KEYCODE_DOWN = 0x40000051,
    RV_KEYCODE_RIGHT = 0x4000004F,
    RV_KEYCODE_LEFT = 0x40000050,
    RV_KEYCODE_LCTRL = 0x400000E0,
    RV_KEYCODE_RCTRL = 0x400000E4,
    RV_KEYCODE_LSHIFT = 0x400000E1,
    RV_KEYCODE_RSHIFT = 0x400000E5,
    RV_KEYCODE_LALT = 0x400000E2,
    RV_KEYCODE_RALT = 0x400000E6,
    RV_KEYCODE_LMETA = 0x400000E3,
    RV_KEYCODE_RMETA = 0x400000E7,
};

typedef struct {
    uint32_t keycode;
    uint8_t state;
    uint16_t mod;
} rv_key_rv_event_t;

typedef struct {
    int32_t x, y, xrel, yrel;
} rv_mouse_motion_t;

enum {
    RV_MOUSE_BUTTON_LEFT = 1,
};

typedef struct {
    uint8_t button;
    uint8_t state;
} rv_mouse_button_t;

enum {
    RV_KEY_EVENT = 0,
    RV_MOUSE_MOTION_EVENT = 1,
    RV_MOUSE_BUTTON_EVENT = 2,
    RV_QUIT_EVENT = 3,
};

typedef struct {
    uint32_t type;
    union {
        rv_key_rv_event_t key_event;
        union {
            rv_mouse_motion_t motion;
            rv_mouse_button_t button;
        } mouse;
    };
} rv_event_t;

enum {
    RV_RELATIVE_MODE_SUBMISSION = 0,
    RV_WINDOW_TITLE_SUBMISSION = 1,
};

typedef struct {
    uint8_t enabled;
} rv_mouse_rv_submission_t;

typedef struct {
    uint32_t title;
    uint32_t size;
} rv_title_rv_submission_t;

typedef struct {
    uint32_t type;
    union {
        rv_mouse_rv_submission_t mouse;
        rv_title_rv_submission_t title;
    };
} rv_submission_t;

FENSTER_API int fenster_open(struct fenster *f)
{
    f->event_queue = malloc((sizeof(rv_event_t) + sizeof(rv_submission_t)) *
                            RV_QUEUE_CAPACITY);
    f->event_count = 0;
    register int a0 __asm("a0") = (int) f->event_queue;
    register int a1 __asm("a1") = RV_QUEUE_CAPACITY;
    register int a2 __asm("a2") = (int) &f->event_count;
    register int a7 __asm("a7") = 0xc0de;
    __asm volatile("scall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
    f->event_queue_start = 0;

    rv_submission_t submission = {
        .type = RV_WINDOW_TITLE_SUBMISSION,
        .title.title = (uint32_t) f->title,
        .title.size = strlen(f->title),
    };
    rv_submission_t *submission_queue =
        (rv_submission_t *) ((rv_event_t *) f->event_queue + RV_QUEUE_CAPACITY);
    submission_queue[0] = submission;
    a0 = 1;
    a7 = 0xfeed;
    __asm volatile("scall" : : "r"(a0), "r"(a7) : "memory");

    return 0;
}

FENSTER_API void fenster_close(struct fenster *f)
{
    free(f->event_queue);
}

FENSTER_API int fenster_loop(struct fenster *f)
{
    for (; f->event_count > 0; f->event_count--) {
        rv_event_t event =
            ((rv_event_t *) f->event_queue)[f->event_queue_start];
        switch (event.type) {
        case RV_KEY_EVENT: {
            uint32_t keycode = event.key_event.keycode;
            uint8_t state = event.key_event.state;

            if (keycode == RV_KEYCODE_RETURN)
                f->keys[10] = state;
            else if (keycode < 128)
                f->keys[keycode] = state;

            if (keycode == RV_KEYCODE_UP)
                f->keys[17] = state;
            if (keycode == RV_KEYCODE_DOWN)
                f->keys[18] = state;
            if (keycode == RV_KEYCODE_RIGHT)
                f->keys[19] = state;
            if (keycode == RV_KEYCODE_LEFT)
                f->keys[20] = state;

            if (keycode == RV_KEYCODE_LCTRL || keycode == RV_KEYCODE_RCTRL)
                f->mod = state ? f->mod | 1 : f->mod & 0b1110;
            if (keycode == RV_KEYCODE_LSHIFT || keycode == RV_KEYCODE_RSHIFT)
                f->mod = state ? f->mod | 2 : f->mod & 0b1101;
            if (keycode == RV_KEYCODE_LALT || keycode == RV_KEYCODE_RALT)
                f->mod = state ? f->mod | 4 : f->mod & 0b1011;
            if (keycode == RV_KEYCODE_LMETA || keycode == RV_KEYCODE_RMETA)
                f->mod = state ? f->mod | 8 : f->mod & 0b0111;

            break;
        }
        case RV_MOUSE_MOTION_EVENT:
            f->x = event.mouse.motion.x;
            f->y = event.mouse.motion.y;
            break;
        case RV_MOUSE_BUTTON_EVENT:
            if (event.mouse.button.button == RV_MOUSE_BUTTON_LEFT)
                f->mouse = event.mouse.button.state;
            break;
        case RV_QUIT_EVENT:
            return -1;
        }
        f->event_queue_start =
            (f->event_queue_start + 1) & (RV_QUEUE_CAPACITY - 1);
    }

    register int a0 __asm("a0") = (uintptr_t) f->buf;
    register int a1 __asm("a1") = f->width;
    register int a2 __asm("a2") = f->height;
    register int a7 __asm("a7") = 0xbeef;
    __asm volatile("scall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
    return 0;
}

#undef RV_QUEUE_CAPACITY

FENSTER_API int64_t fenster_time(void)
{
    return (int64_t) clock() / (CLOCKS_PER_SEC / 1000.0f);
}
FENSTER_API void fenster_sleep(int64_t ms)
{
    int64_t start = fenster_time();
    while (fenster_time() - start < ms)
        ;
}

#ifdef __cplusplus
class Fenster
{
    struct fenster f;
    int64_t now;

public:
    Fenster(const int w, const int h, const char *title)
        : f{.title = title, .width = w, .height = h}
    {
        this->f.buf = new uint32_t[w * h];
        this->now = fenster_time();
        fenster_open(&this->f);
    }
    ~Fenster()
    {
        fenster_close(&this->f);
        delete[] this->f.buf;
    }
    bool loop(const int fps)
    {
        int64_t t = fenster_time();
        if (t - this->now < 1000 / fps) {
            fenster_sleep(t - now);
        }
        this->now = t;
        return fenster_loop(&this->f) == 0;
    }
    inline uint32_t &px(const int x, const int y)
    {
        return fenster_pixel(&this->f, x, y);
    }
    bool key(int c) { return c >= 0 && c < 128 ? this->f.keys[c] : false; }
    int x() { return this->f.x; }
    int y() { return this->f.y; }
    int mouse() { return this->f.mouse; }
    int mod() { return this->f.mod; }
};
#endif /* __cplusplus */

#endif /* !FENSTER_HEADER */
#endif /* FENSTER_H */

#if 0
/* A very minimal example of a Fenster app:
 * - Opens a window
 * - Starts a loop
 * - Changes pixel colours based on some "shader" formula
 * - Sleeps if needed to maintain a certain frame rate
 * - Closes a window
 */
#include "fenster.h"
enum { W = 320, H = 240 };
int main()
{
    uint32_t buf[W * H];
    struct fenster f = {.title = "hello", .width = W, .height = H, .buf = buf};
    fenster_open(&f);
    uint32_t t = 0;
    while (fenster_loop(&f) == 0) {
        t++;
        for (int i = 0; i < 320; i++) {
            for (int j = 0; j < 240; j++)
                fenster_pixel(&f, i, j) = i ^ j ^ t; /* Munching squares */
        }
        fenster_sleep(100);
    }
    fenster_close(&f);
    return 0;
}
#endif
