FROM ubuntu:22.04
LABEL maintainer="henrybear327@gmail.com"

# Install packages required for the emulator to compile and execute correctly
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libsdl2-dev libsdl2-mixer-dev python3-pip git 

RUN python3 -m pip install git+https://github.com/riscv/riscof 

# set up the timezone
ENV TZ=Asia/Taipei
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# when using apt install gcc-riscv64-unknown-elf, this will cause "unsupported ISA subset 'z'" during compilation
# thus, we are building from scratch, following the version here -> https://github.com/sysprog21/rv32emu/blob/master/.ci/riscv-toolchain-install.sh
# for x86, we can optimize this part and take the nightly build directly, but not for aarch64 
ENV RISCV=/opt/riscv
ENV PATH=$RISCV/bin:$PATH
WORKDIR $RISCV
RUN apt install -y autoconf automake autotools-dev curl libmpc-dev libmpfr-dev libgmp-dev gawk build-essential bison flex texinfo gperf libtool patchutils bc zlib1g-dev libexpat-dev
RUN git clone --recursive https://github.com/riscv/riscv-gnu-toolchain
RUN cd riscv-gnu-toolchain && \
    git checkout tags/2023.10.06 && \
    ./configure --prefix=/opt/riscv --with-arch=rv32gc --with-abi=ilp32d && \
    make -j$(nproc) && \
    make clean

# the default reference emulator is x86-based
# we need to build it ourselves if we are using it on aarch64
# https://riscof.readthedocs.io/en/stable/installation.html#install-plugin-models
# the above commands are modified to match the current build flow as indicated in the Github CI -> https://github.com/riscv/sail-riscv/blob/master/.github/workflows/compile.yml
WORKDIR /home/root/
RUN apt install -y opam zlib1g-dev pkg-config libgmp-dev z3 device-tree-compiler
RUN opam init --disable-sandboxing -y
RUN opam install -y sail
RUN git clone https://github.com/riscv/sail-riscv.git
# based on this commit https://github.com/sysprog21/rv32emu/commit/01b00b6f175f57ef39ffd1f4fa6a611891e36df3#diff-3b436c5e32c40ecca4095bdacc1fb69c0759096f86e029238ce34bbe73c6e68f
# we infer that the sail-riscv binary was taken from commit 9547a30bf84572c458476591b569a95f5232c1c7
RUN cd sail-riscv && \
    git checkout 9547a30bf84572c458476591b569a95f5232c1c7 && \
    eval $(opam env) && \
    make && \
    ARCH=RV32 make 

# copy in the source code
WORKDIR /home/root/rv32emu
COPY . .

# replace the emulator (riscv_sim_RV32) with the arch that the container can execute 
RUN rm /home/root/rv32emu/tests/arch-test-target/sail_cSim/riscv_sim_RV32
RUN cp /home/root/sail-riscv/c_emulator/riscv_sim_RV32 /home/root/rv32emu/tests/arch-test-target/sail_cSim/riscv_sim_RV32

# clean up apt cache
RUN rm -rf /var/lib/apt/lists/*
