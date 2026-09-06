# Networking in rv32emu

This document explains how to configure and use virtio-net networking in rv32emu.

rv32emu provides a virtio-net device with platform-specific host networking backends. The currently supported backends are:

- Linux : TAP and user-mode SLIRP
- macOS : user-mode SLIRP
- Emscripten : networking backends are disabled

vmnet.framework support is not included yet.

## Backend overview

### Linux TAP

The TAP backend uses the Linux TUN/TAP interface. It provides kernel-level networking, but requires either `sudo` or `CAP_NET_ADMIN`.

Use this backend when you want direct host-side TAP networking.

### User-mode SLIRP

The user backend uses minislirp for user-mode networking. It does not require root privileges, TAP setup, or host network configuration.

The default guest network is:

| Item | Address |
|------|---------|
| Guest IP | `10.0.2.15/24` |
| Gateway | `10.0.2.2` |
| DNS | `10.0.2.3` |

This backend is supported on Linux and macOS builds.

### Emscripten

Emscripten builds do not enable TAP, SLIRP, or vmnet networking. Unsupported virtio-net backends are rejected during argument parsing.

## Usage

The virtio-net backend is selected with:

```shell
-x vnet:<backend>
```
where `<backend>` is one of the supported backend names for the current host.

### Linux TAP mode

TAP mode requires root privileges or `CAP_NET_ADMIN`.

Start rv32emu:

```
sudo -E build/rv32emu \
  -k build/linux-image/Image \
  -i build/linux-image/rootfs.cpio \
  -x vnet:tap
```

rv32emu allocates a TAP interface, for example `tap0`.

In another terminal, configure the host TAP interface:

```
sudo ip addr replace 192.168.100.1/24 dev tap0
sudo ip link set tap0 up
```

Inside the guest:

```
ip link set eth0 up
ip addr flush dev eth0
ip addr add 192.168.100.2/24 dev eth0
ping -c 3 192.168.100.1
```

### Linux user-mode SLIRP

User-mode SLIRP does not require root privileges.

Start rv32emu:

```
build/rv32emu \
  -k build/linux-image/Image \
  -i build/linux-image/rootfs.cpio \
  -x vnet:user
```

Inside the guest:

```
ip link set eth0 up
ip addr flush dev eth0
ip addr add 10.0.2.15/24 dev eth0
ip route add default via 10.0.2.2
ping -c 3 10.0.2.2
```

The `10.0.2.2` address is the SLIRP gateway.

### macOS user-mode SLIRP

On macOS, use the user-mode SLIRP backend:

```
build/rv32emu \
  -k build/linux-image/Image \
  -i build/linux-image/rootfs.cpio \
  -x vnet:user
```

Inside the guest, use the same static configuration as Linux user mode:

```
ip link set eth0 up
ip addr flush dev eth0
ip addr add 10.0.2.15/24 dev eth0
ip route add default via 10.0.2.2
ping -c 3 10.0.2.2
```

## Testing

The CI tests validate virtio-net through the existing Linux boot flow.

Linux x64 interpreter jobs run both:

```
VNET_BACKEND=user .ci/boot-linux.sh
VNET_BACKEND=tap sudo -E .ci/boot-linux.sh
```

macOS runs the user-mode SLIRP boot test:

Shell

```
VNET_BACKEND=user .ci/boot-linux.sh
```

TAP is only tested on Linux because it depends on the Linux TUN/TAP interface.

The virtio-net tests are not run for every JIT, T2C, or MOP-fusion matrix entry. Those modes already have normal Linux boot coverage. The virtio-net tests mainly validate backend I/O, virtio-net driver binding, guest interface setup, RX/TX virtqueues, and interrupt delivery.

## Implementation notes

The virtio-net implementation is split into:

| File | Purpose |
| --- | --- |
| `src/devices/virtio-net.c` | VirtIO-Net device emulation |
| `src/devices/netdev.h` | Network backend abstraction |
| `src/devices/netdev.c` | Backend selection and initialization |
| `src/devices/slirp.c` | minislirp integration for user-mode networking |

The user-mode SLIRP backend uses non-blocking socketpairs to connect the virtio-net RX/TX paths with minislirp.

At the moment, SLIRP progress is driven from rv32emu's existing virtio-net refresh path. A future improvement could replace frequent non-blocking polling with an event-driven wakeup mechanism, such as `eventfd`, a pipe, or a condition-variable based notification path.

## Future work

### vmnet.framework

macOS vmnet.framework support is not implemented in rv32emu yet.

semu has a vmnet backend, but that implementation uses Apple's vmnet.framework and Blocks syntax for callbacks. This is suitable for Clang with Blocks support, but it is not suitable for the current macOS gcc-15 CI job. vmnet also has macOS-specific privilege and entitlement requirements.

A future implementation could add vmnet through a Clang-only macOS path or a more portable wrapper.

### Event-driven wakeup

The current backend uses non-blocking polling from the emulator refresh path. This keeps the initial backend simple and consistent with rv32emu's current device model.

A future implementation could use an event-driven wakeup path so host-side activity can wake the emulator loop directly.