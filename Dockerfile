FROM alpine:3.19 as base_gcc

RUN apk add --update alpine-sdk git curl

# copy in the source code
WORKDIR /home/root/rv32emu
COPY . .

# generate execution file for rv32emu and rv_histogram
RUN make ENABLE_SDL=0
RUN make tool

FROM alpine:3.19 as final

# copy in elf files
COPY ./build/*.elf /home/root/rv32emu/build/

# get rv32emu and rv_histogram binaries
COPY --from=base_gcc /home/root/rv32emu/build/rv32emu /home/root/rv32emu/build/rv32emu
COPY --from=base_gcc /home/root/rv32emu/build/rv_histogram /home/root/rv32emu/build/rv_histogram

ENV PATH=/home/root/rv32emu/build:$PATH

WORKDIR /home/root/rv32emu
