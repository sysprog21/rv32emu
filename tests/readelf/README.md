# readelf

A simple ELF format inspect tool without any dependencies.

## Implemented Features

- ELF file header (with `-h` or `-a` options)
- ELF sections' header (with `-S` option)

## Getting Started

1. Clone this repo to local.
  ```sh
  git clone https://github.com/ghosind/readelf
  ```
2. Run `make` to compile program.
  ```sh
  make
  ```
3. Run `bin/readelf` to inspect some elf-format files.
  ```sh
  bin/readelf -a <path_of_elf_format_file>
  ```

## Documentations and Resources

- [Portable Formats Specification, Version 1.1](https://www.cs.cmu.edu/afs/cs/academic/class/15213-f00/docs/elf.pdf)
- [readelf manual](https://man7.org/linux/man-pages/man1/readelf.1.html)
- [Implementation in binutils](https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=binutils/readelf.c)
