FROM sysprog21/rv32emu-gcc as base_gcc
FROM sysprog21/rv32emu-sail as base_sail

FROM ubuntu:22.04 as final

# Install extra packages for the emulator to compile and execute with full capabilities correctly
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libsdl2-dev libsdl2-mixer-dev python3-pip git && \
    rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install git+https://github.com/riscv/riscof 

# set up the timezone
ENV TZ=Asia/Taipei
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# copy in the source code
WORKDIR /home/root/rv32emu
COPY . .

# Copy the GNU Toolchain files
ENV RISCV=/opt/riscv
ENV PATH=$RISCV/bin:$PATH
COPY --from=base_gcc /opt/riscv/ /opt/riscv/

# replace the emulator (riscv_sim_RV32) with the arch that the container can execute 
RUN rm /home/root/rv32emu/tests/arch-test-target/sail_cSim/riscv_sim_RV32
COPY --from=base_sail /home/root/riscv_sim_RV32 /home/root/rv32emu/tests/arch-test-target/sail_cSim/riscv_sim_RV32
