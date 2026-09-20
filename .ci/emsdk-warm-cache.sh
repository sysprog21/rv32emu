#!/usr/bin/env bash

# Populate the Emscripten port cache before any parallel build touches it.
#
# On a cold cache, "make -j" trips emscripten's nested-lock assertion: building
# sdl2_mixer spawns one "emcc -c" per source file with -sUSE_SDL=2, and each
# child tries to build the sdl2 port while the parent emcc still holds the cache
# lock and has exported EM_CACHE_IS_LOCKED=1 into its environment. Seeding the
# entries serially turns those child lookups into hits.

set -e -u -o pipefail

embuilder build sdl2 sdl2-mt libmimalloc libmimalloc-mt

# Link rather than just compile: some sysroot libraries (libmimalloc among them)
# only materialize at link time.
WARMUP=$(mktemp -d)
trap 'rm -rf "${WARMUP}"' EXIT
echo 'int main(void) { return 0; }' > "${WARMUP}/warmup.c"
emcc -sSTRICT=0 -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sUSE_ZLIB=1 \
    -sSDL2_MIXER_FORMATS=wav,mid -sMALLOC=mimalloc -pthread \
    -O2 "${WARMUP}/warmup.c" -o "${WARMUP}/warmup.js"
