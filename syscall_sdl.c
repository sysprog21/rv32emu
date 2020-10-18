#ifndef ENABLE_SDL
#error "Do not manage to build this file unless you enable SDL support."
#endif

#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "state.h"

static SDL_Window *sdlWindow = NULL;
static SDL_Renderer *sdlRenderer;
static SDL_Texture *texture;

static bool check_sdl(struct riscv_t *rv, uint32_t width, uint32_t height)
{
    // check if video has been setup
    if (!sdlWindow) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "Failed to call SDL_Init()\n");
            exit(1);
        }

        sdlWindow = SDL_CreateWindow("rv32emu", SDL_WINDOWPOS_UNDEFINED,
                                     SDL_WINDOWPOS_UNDEFINED, width, height,
                                     0 /* flags */);
        if (!sdlWindow) {
            fprintf(stderr, "Window could not be created! SDL_Error: %s\n",
                    SDL_GetError());
            exit(1);
        }

        sdlRenderer =
            SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED);
        texture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);
    }

    // run a simple event handler
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
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
    // access userdata
    state_t *s = rv_userdata(rv);

    // draw(screen, width, height);
    const uint32_t screen = rv_get_reg(rv, rv_reg_a0);
    const uint32_t width = rv_get_reg(rv, rv_reg_a1);
    const uint32_t height = rv_get_reg(rv, rv_reg_a2);

    // check if we need to setup SDL
    if (!check_sdl(rv, width, height))
        return;

    // read directly into video memory
    int pitch = 0;
    void *pixelsPtr;
    if (SDL_LockTexture(texture, NULL, &pixelsPtr, &pitch))
        exit(-1);
    memory_read(s->mem, (uint8_t *) pixelsPtr, screen, width * height * 4);
    SDL_UnlockTexture(texture);

    SDL_Rect r = {0, 0, width, height};
    SDL_RenderCopy(sdlRenderer, texture, NULL, &r);
    SDL_RenderPresent(sdlRenderer);
}

void syscall_draw_frame_pal(struct riscv_t *rv)
{
    // access userdata
    state_t *s = rv_userdata(rv);

    // draw(screen, width, height);
    const uint32_t buf = rv_get_reg(rv, rv_reg_a0);
    const uint32_t pal = rv_get_reg(rv, rv_reg_a1);
    const uint32_t width = rv_get_reg(rv, rv_reg_a2);
    const uint32_t height = rv_get_reg(rv, rv_reg_a3);

    // check if we need to setup SDL
    if (!check_sdl(rv, width, height))
        return;

    // read directly into video memory
    uint8_t *i = (uint8_t *) malloc(width * height);
    uint8_t *j = (uint8_t *) malloc(256 * 3);

    memory_read(s->mem, i, buf, width * height);
    memory_read(s->mem, j, pal, 256 * 3);

    int pitch = 0;
    void *pixelsPtr;
    if (SDL_LockTexture(texture, NULL, &pixelsPtr, &pitch))
        exit(-1);
    uint32_t *d = (uint32_t *) pixelsPtr;
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

    SDL_Rect r = {0, 0, width, height};
    SDL_RenderCopy(sdlRenderer, texture, NULL, &r);
    SDL_RenderPresent(sdlRenderer);

    free(i);
    free(j);
}
