FROM ubuntu:20.04

# Set timezone, otherwise it can takes a rather long time to setup
# Adjust this according to your timezone
ENV TZ=Asia/Taipei
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update && apt-get install -y \
  git \
  build-essential \
  libsdl2-dev \
  gcc-riscv64-unknown-elf

COPY . /usr/src/rv32emu
WORKDIR /usr/src/rv32emu
