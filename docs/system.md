# System emulation

`rv32emu` provides experimental system emulation capable of booting an RV32
Linux kernel and running user-space binaries. This document covers building
and running system mode, attaching virtio block devices, customizing
bootargs, and building a Linux image from source.

Device Tree compiler (dtc) is required:
* Debian/Ubuntu Linux: `sudo apt install device-tree-compiler`
* macOS: `brew install dtc`

## Build and run

Build and run using default images (fetched from
[rv32emu-prebuilt](https://github.com/sysprog21/rv32emu-prebuilt) before
running). If `ENABLE_ARCH_TEST=1` was previously set, run `make distclean`
before proceeding.
```shell
$ make ENABLE_SYSTEM=1 system
```

For improved performance, JIT compilation can be enabled in system emulation mode:
```shell
$ make system_jit_defconfig
$ make system
```

Build and run using specified images (`readonly` option makes the virtual
block device read-only):
```shell
$ make ENABLE_SYSTEM=1
$ build/rv32emu -k <kernel_img_path> -i <rootfs_img_path> [-x vblk:<virtio_blk_img_path>[,readonly]]
```

`mk/system.mk` auto-sizes the initrd region from the on-disk `rootfs.cpio`
when present (file size + 2 MiB), falling back to 32 MiB. Override
`INITRD_SIZE` on the make line for SDL-oriented workloads that ship larger
asset bundles:
```shell
$ make system ENABLE_SYSTEM=1 ENABLE_SDL=1 INITRD_SIZE=64
```
Once logged into the guestOS, run `doom-riscv` or `quake` or `smolnes`. To
terminate SDL-oriented applications, use the built-in exit utility, ctrl-c
or the SDL window close button(X).

## Virtio block device (optional)

Generate ext4 image file for virtio block device in Unix-like system:
```shell
$ dd if=/dev/zero of=disk.img bs=4M count=32
$ mkfs.ext4 disk.img
```
Instead of creating a new block device image, you can share the hostOS's
existing block devices. For example, on macOS host, specify the block device
path as `-x vblk:/dev/disk3`, or on Linux host as `-x vblk:/dev/loop3`,
assuming these paths point to valid block devices.

Mount the virtual block device and create a test file after booting, note
that root privilege is required to mount and unmount a disk:
```shell
# mkdir mnt
# mount /dev/vda mnt
# echo "rv32emu" > mnt/emu.txt
# umount mnt
```
Reboot and re-mount the virtual block device, the written file should
remain existing.

To specify multiple virtual block devices, pass multiple `-x vblk` options
when launching the emulator. Each option can point to either a disk image
or a hostOS block device, with optional read-only mode. For example:
```shell
$ build/rv32emu -k <kernel_img_path> -i <rootfs_img_path> -x vblk:disk.img -x vblk:/dev/loop22,readonly
```
Note that the /dev/vdx device order in guestOS is assigned in reverse: the
first `-x vblk` argument corresponds to the device with the highest letter,
while subsequent arguments receive lower-lettered device names.

### Out-of-tree filesystems

In addition to the built-in ext4 filesystem support, other out-of-tree
filesystems such as [simplefs](https://github.com/sysprog21/simplefs) are
also supported. To use simplefs, first follow the instructions
[here](https://github.com/sysprog21/simplefs?tab=readme-ov-file#build-and-run)
to generate the simplefs disk image, and then attach it to the guestOS via
the virtio block device. An additional ext4 image containing `simplefs.ko`
must also be attached to the guestOS, since `simplefs.ko` is out-of-tree
kernel module. The pre-built `simplefs.ko` can be found at
`build/linux-image/` (run `make artifact ENABLE_SYSTEM=1` to get the
artifacts).
```shell
$ build/rv32emu -k <kernel_img_path> -i <rootfs_img_path> -x vblk:<ext4_disk_img_path> -x vblk:<simplefs_disk_img_path>
```
Once the guestOS is booted, insert the `simplefs.ko` kernel module. After
loading the kernel module, the simplefs disk will be recognized by the
Linux kernel and can be mounted and used as a regular filesystem:
```shell
# mkdir -p mnt && mount /dev/vdb mnt # mount the ext4 disk that contains simplefs.ko

# insmod mnt/simplefs.ko # insert simplefs.ko

# mkdir -p simplefs && mount -t simplefs /dev/vda simplefs # mount the simplefs disk
```

## Customize bootargs

Build and run with customized bootargs to boot the guestOS. Otherwise, the
default bootargs defined in `src/devices/minimal.dts` will be used:
```shell
$ build/rv32emu -k <kernel_img_path> -i <rootfs_img_path> [-b <bootargs>]
```

## Build Linux image from source

An automated build script is provided to compile the RISC-V cross-compiler,
Busybox, and Linux kernel from source. Please note that it only supports the
Linux host environment. It can be found at `tools/build-linux-image.sh`.
```shell
$ make build-linux-image
```
