#ifndef ENABLE_SDL
#error "Do not manage to build this file unless you enable SDL support."
#endif

#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "state.h"

/* For optimization, the capcity of event queues must be the power of two to
 * avoid the expensive modulo operation, the details are explained here:
 * https://stackoverflow.com/questions/10527581/why-must-a-ring-buffer-size-be-a-power-of-2
 */
#define EVENT_QUEUE_CAPACITY 128

enum {
    KEY_EVENT = 0,
    MOUSE_MOTION_EVENT = 1,
    MOUSE_BUTTON_EVENT = 2,
};

typedef struct {
    uint32_t keycode;
    uint8_t state;
} key_event_t;

typedef struct {
    int32_t xrel, yrel;
} mouse_motion_t;

typedef struct {
    uint8_t button;
    uint8_t state;
} mouse_button_t;

typedef struct {
    uint32_t type;
    union {
        key_event_t key_event;
        union {
            mouse_motion_t motion;
            mouse_button_t button;
        } mouse;
    };
} event_t;

typedef struct {
    event_t events[EVENT_QUEUE_CAPACITY];
    size_t start, end;
    bool full;
} event_queue_t;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static event_queue_t event_queue = {
    .events = {},
    .start = 0,
    .end = 0,
    .full = false,
};

static bool event_pop(event_t *event)
{
    if (event_queue.start == event_queue.end)
        return false;
    *event = event_queue.events[event_queue.start++];
    event_queue.start &= EVENT_QUEUE_CAPACITY - 1;
    event_queue.full = false;
    return true;
}

static void event_push(event_t event)
{
    if (event_queue.full)
        return;
    event_queue.events[event_queue.end++] = event;
    event_queue.end &= EVENT_QUEUE_CAPACITY - 1;
    event_queue.full = (event_queue.start == event_queue.end);
}

/* check if we need to setup SDL and run event loop */
static bool check_sdl(struct riscv_t *rv, uint32_t width, uint32_t height)
{
    if (!window) { /* check if video has been initialized. */
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "Failed to call SDL_Init()\n");
            exit(1);
        }

        window = SDL_CreateWindow("rv32emu", SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, width, height,
                                  0 /* flags */);
        if (!window) {
            fprintf(stderr, "Window could not be created! SDL_Error: %s\n",
                    SDL_GetError());
            exit(1);
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) { /* Run event handler */
        switch (event.type) {
        case SDL_QUIT:
            rv_halt(rv);
            return false;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE &&
                SDL_GetRelativeMouseMode() == SDL_TRUE) {
                SDL_SetRelativeMouseMode(SDL_FALSE);
                break;
            }
            /* fall through */
        case SDL_KEYUP: {
            if (event.key.repeat)
                break;
            event_t new_event = {
                .type = KEY_EVENT,
            };
            key_event_t key_event = {
                .keycode = event.key.keysym.sym,
                .state = (bool) (event.key.state == SDL_PRESSED),
            };
            memcpy(&new_event.key_event, &key_event, sizeof(key_event));
            event_push(new_event);
            break;
        }
        case SDL_MOUSEMOTION: {
            event_t new_event = {
                .type = MOUSE_MOTION_EVENT,
            };
            mouse_motion_t mouse_motion = {
                .xrel = event.motion.xrel,
                .yrel = event.motion.yrel,
            };
            memcpy(&new_event.mouse.motion, &mouse_motion,
                   sizeof(mouse_motion));
            event_push(new_event);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT &&
                SDL_GetRelativeMouseMode() == SDL_FALSE) {
                SDL_SetRelativeMouseMode(SDL_TRUE);
                break;
            }
            /* fall through */
        case SDL_MOUSEBUTTONUP: {
            event_t new_event = {
                .type = MOUSE_BUTTON_EVENT,
            };
            mouse_button_t mouse_button = {
                .button = event.button.button,
                .state = (bool) (event.button.state == SDL_PRESSED),
            };
            memcpy(&new_event.mouse.button, &mouse_button,
                   sizeof(mouse_button));
            event_push(new_event);
            break;
        }
        }
    }
    return true;
}

void syscall_draw_frame(struct riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* draw_frame(screen, width, height) */
    const uint32_t screen = rv_get_reg(rv, rv_reg_a0);
    const uint32_t width = rv_get_reg(rv, rv_reg_a1);
    const uint32_t height = rv_get_reg(rv, rv_reg_a2);

    if (!check_sdl(rv, width, height))
        return;

    /* read directly into video memory */
    int pitch = 0;
    void *pixels_ptr;
    if (SDL_LockTexture(texture, NULL, &pixels_ptr, &pitch))
        exit(-1);
    memory_read(s->mem, pixels_ptr, screen, width * height * 4);
    SDL_UnlockTexture(texture);

    SDL_RenderCopy(renderer, texture, NULL, &(SDL_Rect){0, 0, width, height});
    SDL_RenderPresent(renderer);
}

void syscall_draw_frame_pal(struct riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* draw_frame_pal(screen, pal, width, height) */
    const uint32_t screen = rv_get_reg(rv, rv_reg_a0);
    const uint32_t pal = rv_get_reg(rv, rv_reg_a1);
    const uint32_t width = rv_get_reg(rv, rv_reg_a2);
    const uint32_t height = rv_get_reg(rv, rv_reg_a3);

    if (!check_sdl(rv, width, height))
        return;

    uint8_t *i = malloc(width * height);
    uint8_t *j = malloc(256 * 3);

    memory_read(s->mem, i, screen, width * height);
    memory_read(s->mem, j, pal, 256 * 3);

    int pitch = 0;
    void *pixels_ptr;
    if (SDL_LockTexture(texture, NULL, &pixels_ptr, &pitch))
        exit(-1);
    uint32_t *d = pixels_ptr;
    const uint8_t *p = i;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const uint8_t c = p[x];
            const uint8_t *lut = j + (c * 3);
            d[x] = (lut[0] << 16) | (lut[1] << 8) | lut[2];
        }
        p += width, d += width;
    }
    SDL_UnlockTexture(texture);

    SDL_RenderCopy(renderer, texture, NULL, &(SDL_Rect){0, 0, width, height});
    SDL_RenderPresent(renderer);

    free(i);
    free(j);
}

void syscall_poll_event(struct riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* poll_event(event) */
    const uint32_t base = rv_get_reg(rv, rv_reg_a0);

    event_t event;
    if (!event_pop(&event)) {
        rv_set_reg(rv, rv_reg_a0, 0);
        return;
    }

    memory_write(s->mem, base + 0, (const uint8_t *) &event.type, 4);
    switch (event.type) {
    case KEY_EVENT:
        memory_write(s->mem, base + 4, (const uint8_t *) &event.key_event,
                     sizeof(key_event_t));
        break;
    case MOUSE_MOTION_EVENT:
        memory_write(s->mem, base + 4, (const uint8_t *) &event.mouse.motion,
                     sizeof(mouse_motion_t));
        break;
    case MOUSE_BUTTON_EVENT:
        memory_write(s->mem, base + 4, (const uint8_t *) &event.mouse.button,
                     sizeof(mouse_button_t));
        break;
    }

    rv_set_reg(rv, rv_reg_a0, 1);
}