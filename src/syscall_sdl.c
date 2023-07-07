/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#if !RV32_HAS(SDL)
#error "Do not manage to build this file unless you enable SDL support."
#endif

#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "state.h"

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
    uint32_t base;
    size_t end;
} event_queue_t;

enum {
    RELATIVE_MODE_SUBMISSION = 0,
};

typedef struct {
    uint32_t type;
    union {
        union {
            uint8_t enabled;
        } mouse;
    };
} submission_t;

typedef struct {
    uint32_t base;
    size_t start;
} submission_queue_t;

/* SDL-related variables */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

/* Event queue specific variables */
static uint32_t queues_capacity;
static uint32_t event_count;
static uint32_t deferred_submissions = 0;
static event_queue_t event_queue = {
    .base = 0,
    .end = 0,
};
static submission_queue_t submission_queue = {
    .base = 0,
    .start = 0,
};

static submission_t submission_pop(riscv_t *rv)
{
    state_t *s = rv_userdata(rv);
    submission_t submission;
    memory_read(
        s->mem, (void *) &submission,
        submission_queue.base + submission_queue.start * sizeof(submission_t),
        sizeof(submission_t));
    ++submission_queue.start;
    submission_queue.start &= queues_capacity - 1;
    return submission;
}

static void event_push(riscv_t *rv, event_t event)
{
    state_t *s = rv_userdata(rv);
    memory_write(s->mem, event_queue.base + event_queue.end * sizeof(event_t),
                 (void *) &event, sizeof(event_t));
    ++event_queue.end;
    event_queue.end &= queues_capacity - 1;

    uint32_t count;
    memory_read(s->mem, (void *) &count, event_count, sizeof(uint32_t));
    count += 1;
    memory_write(s->mem, event_count, (void *) &count, sizeof(uint32_t));
}

static inline uint32_t round_pow2(uint32_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    x = 1 << (32 - __builtin_clz(x - 1));
#else
    /* Bit Twiddling Hack */
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;
#endif
    return x;
}

void syscall_submit_queue(riscv_t *rv);

/* check if SDL needs to be set up and run the event loop */
static bool check_sdl(riscv_t *rv, int width, int height)
{
    if (!window) { /* check if video has been initialized. */
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "Failed to call SDL_Init()\n");
            exit(1);
        }
        window = SDL_CreateWindow("rv32emu", SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, width, height,
                                  SDL_WINDOW_RESIZABLE);
        if (!window) {
            fprintf(stderr, "Window could not be created! SDL_Error: %s\n",
                    SDL_GetError());
            exit(1);
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);

        if (deferred_submissions) {
            syscall_submit_queue(rv);
            deferred_submissions = 0;
        }
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) { /* Run event handler */
        switch (event.type) {
        case SDL_QUIT:
            rv_halt(rv);
            return false;
        case SDL_KEYDOWN:
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
            event_push(rv, new_event);
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
            event_push(rv, new_event);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
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
            event_push(rv, new_event);
            break;
        }
        default:
            break;
        }
    }
    return true;
}

void syscall_draw_frame(riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* draw_frame(base, width, height) */
    const uint32_t screen = rv_get_reg(rv, rv_reg_a0);
    const int width = rv_get_reg(rv, rv_reg_a1);
    const int height = rv_get_reg(rv, rv_reg_a2);

    if (!check_sdl(rv, width, height))
        return;

    /* read directly into video memory */
    int pitch = 0;
    void *pixels_ptr;
    if (SDL_LockTexture(texture, NULL, &pixels_ptr, &pitch))
        exit(-1);
    memory_read(s->mem, pixels_ptr, screen, width * height * 4);
    SDL_UnlockTexture(texture);

    int actual_width, actual_height;
    SDL_GetWindowSize(window, &actual_width, &actual_height);
    SDL_RenderCopy(renderer, texture, NULL,
                   &(SDL_Rect){0, 0, actual_width, actual_height});
    SDL_RenderPresent(renderer);
}

void syscall_setup_queue(riscv_t *rv)
{
    /* setup_queue(base, capacity, event_count) */
    uint32_t base = rv_get_reg(rv, rv_reg_a0);
    queues_capacity = rv_get_reg(rv, rv_reg_a1);
    event_count = rv_get_reg(rv, rv_reg_a2);

    event_queue.base = base;
    submission_queue.base = base + sizeof(event_t) * queues_capacity;
    queues_capacity = round_pow2(queues_capacity);
}

void syscall_submit_queue(riscv_t *rv)
{
    /* submit_queue(count) */
    uint32_t count = rv_get_reg(rv, rv_reg_a0);

    if (!window) {
        deferred_submissions += count;
        return;
    }

    if (deferred_submissions)
        count = deferred_submissions;

    while (count--) {
        submission_t submission = submission_pop(rv);

        switch (submission.type) {
        case RELATIVE_MODE_SUBMISSION:
            SDL_SetRelativeMouseMode(submission.mouse.enabled);
            break;
        }
    }
}
