# Docker image

The image containing all the necessary tools for development and testing
can be executed by `docker run -it sysprog21/rv32emu:latest`. It works for
both x86-64 and aarch64 (e.g., Apple's M1 chip) machines.

To keep the Docker image minimal, executables and prebuilt images are not
embedded. Follow the steps below to fetch the required artifacts.

## User-mode

Fetch and extract the latest prebuilt user-mode ELF artifacts:
```shell
$ wget $(wget -qO- 'https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases?per_page=100' \
  | grep browser_download_url | grep ELF | head -n 1 | cut -d '"' -f4)

$ tar -xzvf rv32emu-prebuilt.tar.gz
```
To run `rv32emu-user`, consider the following examples:
```shell
# Run a local ELF program
build/rv32emu-user build/hello.elf

# Run a prebuilt user-mode program (e.g., pi)
build/rv32emu-user rv32emu-prebuilt/riscv32/pi
```

## System-mode

Fetch and extract the latest prebuilt system-mode Linux image artifacts:
```shell
$ wget $(wget -qO- 'https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases?per_page=100' \
  | grep browser_download_url | grep Linux-Image | head -n 1 | cut -d '"' -f4)

$ tar -xzvf rv32emu-linux-image-prebuilt.tar.gz
```
To run `rv32emu-system`, use the following:
```shell
$ build/rv32emu-system \
  -k rv32emu-linux-image-prebuilt/linux-image/Image \
  -i rv32emu-linux-image-prebuilt/linux-image/rootfs.cpio
```
