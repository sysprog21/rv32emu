FROM alpine:3.21 AS base_gcc

RUN apk add --update alpine-sdk git wget sdl2-dev sdl2_mixer-dev dtc

# copy in the source code
WORKDIR /home/root/rv32emu
COPY . .

# build and rename executables: rv32emu-user, rv32emu-system, and rv32emu-histogram
RUN make -j"$(nproc)"
RUN make tool -j"$(nproc)"
RUN mv ./build/rv32emu ./build/rv32emu-user
RUN mv ./build/rv_histogram ./build/rv32emu-histogram
RUN make distclean
RUN make ENABLE_SYSTEM=1 INITRD_SIZE=32 -j"$(nproc)"

FROM alpine:3.21 AS final

# copy in elf files
COPY ./build/*.elf /home/root/rv32emu/build/

# get rv32emu and rv_histogram binaries and lib of SDL2 and SDL2_mixer
COPY --from=base_gcc /usr/include/SDL2/ /usr/include/SDL2/
COPY --from=base_gcc /usr/lib/libSDL2* /usr/lib/
COPY --from=base_gcc /home/root/rv32emu/build/rv32emu-user /home/root/rv32emu/build/rv32emu-user
COPY --from=base_gcc /home/root/rv32emu/build/rv32emu /home/root/rv32emu/build/rv32emu-system
COPY --from=base_gcc /home/root/rv32emu/build/rv32emu-histogram /home/root/rv32emu/build/rv32emu-histogram

ENV PATH=/home/root/rv32emu/build:$PATH

WORKDIR /home/root/rv32emu
