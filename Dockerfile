FROM sysprog21/rv32emu-sail as base_sail

FROM ubuntu:22.04 as base_gcc

# install extra packages for the emulator to compile with full capabilities correctly
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential curl git && \
    rm -rf /var/lib/apt/lists/*

# copy in the source code
WORKDIR /home/root/rv32emu
COPY . .

# generate execution file for rv32emu and rv_histogram
RUN make
RUN make tool

FROM ubuntu:22.04 as final

# set up the timezone
ENV TZ=Asia/Taipei
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# copy in elf files and reference emulator
WORKDIR /home/root/rv32emu
COPY ./build/*.elf /home/root/rv32emu/build/
COPY ./tests/arch-test-target/sail_cSim/riscv_sim_RV32 /home/root/rv32emu/tests/arch-test-target/sail_cSim/

# replace the emulator (riscv_sim_RV32) with the arch that the container can execute 
RUN rm /home/root/rv32emu/tests/arch-test-target/sail_cSim/riscv_sim_RV32
COPY --from=base_sail /home/root/riscv_sim_RV32 /home/root/rv32emu/tests/arch-test-target/sail_cSim/

# get rv32emu and rv_histogram binaries
COPY --from=base_gcc /home/root/rv32emu/build/rv32emu /home/root/rv32emu/build/rv32emu
COPY --from=base_gcc /home/root/rv32emu/build/rv_histogram /home/root/rv32emu/build/rv_histogram

ENV PATH=/home/root/rv32emu/build:/home/root/rv32emu/tests/arch-test-target/sail_cSim:$PATH
