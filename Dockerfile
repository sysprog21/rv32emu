FROM alpine:3.21 AS base_gcc

RUN apk add --update alpine-sdk git wget sdl2-dev sdl2_mixer-dev

# copy in the source code
WORKDIR /home/root/rv32emu
COPY . .

# generate execution file for rv32emu and rv_histogram
RUN make
RUN make tool

FROM alpine:3.19 AS final

# copy in elf files
COPY ./build/*.elf /home/root/rv32emu/build/

# get rv32emu and rv_histogram binaries and lib of SDL2 and SDL2_mixer
COPY --from=base_gcc /usr/include/SDL2/ /usr/include/
COPY --from=base_gcc /usr/lib/libSDL2* /usr/lib/
COPY --from=base_gcc /home/root/rv32emu/build/rv32emu /home/root/rv32emu/build/rv32emu
COPY --from=base_gcc /home/root/rv32emu/build/rv_histogram /home/root/rv32emu/build/rv_histogram

ENV PATH=/home/root/rv32emu/build:$PATH

WORKDIR /home/root/rv32emu
