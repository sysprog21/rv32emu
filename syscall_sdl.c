#ifndef ENABLE_SDL
#error "Do not manage to build this file unless you enable SDL support."
#endif

#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "state.h"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

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
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                rv_halt(rv);
                return false;
            }
        }
    }
    return true;
}

void syscall_draw_frame(struct riscv_t *rv)
{
    state_t *s = rv_userdata(rv); /* access userdata */

    /* draw(screen, width, height) */
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

    /* draw(screen, width, height) */
    const uint32_t buf = rv_get_reg(rv, rv_reg_a0);
    const uint32_t pal = rv_get_reg(rv, rv_reg_a1);
    const uint32_t width = rv_get_reg(rv, rv_reg_a2);
    const uint32_t height = rv_get_reg(rv, rv_reg_a3);

    if (!check_sdl(rv, width, height))
        return;

    /* read directly into video memory */
    uint8_t *i = malloc(width * height);
    uint8_t *j = malloc(256 * 3);

    memory_read(s->mem, i, buf, width * height);
    memory_read(s->mem, j, pal, 256 * 3);

    int pitch = 0;
    void *pixels_ptr;
    if (SDL_LockTexture(texture, NULL, &pixels_ptr, &pitch))
        exit(-1);
    uint32_t *d = pixels_ptr;
    const uint8_t *p = i;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < height; ++x) {
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
