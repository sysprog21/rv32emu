# VirtIO sound

`rv32emu` can expose a VirtIO sound playback device to a Linux guest in
system-emulation mode. The host side uses PortAudio, while the guest uses the
standard Linux `virtio_snd` driver and ALSA.

## Host requirements

VirtIO sound is optional. Install PortAudio and `pkg-config` before enabling it.

Ubuntu / Debian:

```sh
sudo apt install portaudio19-dev pkg-config
```

macOS with Homebrew:

```sh
brew install portaudio pkg-config
```

The Kconfig option is available only when PortAudio is detected through
`pkg-config`.

## Build configuration

The system defconfig enables VirtIO sound:

```sh
make system_defconfig
make
```

For an interactive configuration:

```sh
make config
```

Then enable:

```text
System Emulation Mode
└── Enable VirtIO sound device
```

The corresponding Kconfig symbol is:

```text
CONFIG_VIRTIO_SND=y
```

When disabled, `virtio-snd.o` is excluded and PortAudio is not linked into the
emulator.

## Boot a Linux guest with VirtIO sound

Build or fetch the Linux system image first, then run:

```sh
build/rv32emu \
  -k build/linux-image/Image \
  -i build/linux-image/rootfs.cpio \
  -x vsnd
```

The `-x vsnd` option creates one VirtIO sound playback device.

Inside the guest, verify that the Linux driver is bound:

```sh
readlink /sys/bus/virtio/devices/virtio0/driver
cat /proc/asound/cards
aplay -l
```

A typical result contains:

```text
virtio_snd
VirtIO SoundCard
```

## Playback

The current implementation provides playback output through the host's default
PortAudio output device. A simple guest-side test is:

```sh
speaker-test -D hw:0,0 -c 1 -r 48000 -F S16_LE -t sine
```

To play a WAV file:

```sh
aplay -D hw:0,0 song.wav
```

The current implementation advertises one playback stream, one channel, S16 PCM,
and the VirtIO sound rate set implemented by `virtio-snd.c`.

## Buffer and completion model

Each guest playback request is retained by the device until its PCM payload has
been consumed by the host audio callback.

```text
Linux ALSA period
    |
    v
VirtIO TX available ring
    |
    v
rv32emu PCM queue
    |
    v
PortAudio callback
    |
    v
completed queue
    |
    v
VirtIO TX used ring + interrupt
```

This keeps descriptor ownership aligned with PCM consumption. The host callback
does not block when the PCM queue is empty; it writes silence and continues.

There is no device-side fixed prebuffer threshold. Linux may submit playback
periods during `PREPARE`, and host playback starts when the guest sends
`PCM_START`.

## CI coverage

`.ci/test-sound.sh` is a headless smoke test. It boots Linux with `-x vsnd` and
checks:

- the `virtio_snd` driver binding;
- ALSA card enumeration;
- ALSA PCM playback-device enumeration.

GitHub-hosted runners do not provide a reliable physical audio output device, so
CI intentionally does not require audible playback. Real PCM playback remains a
manual integration test.

## Current scope

The current device model is intentionally small:

- one VirtIO sound device;
- playback only;
- one PCM stream;
- mono audio;
- S16 PCM samples;
- PortAudio default output device;
- no capture path.

These constraints can be extended independently from the VirtIO queue lifecycle
and Kconfig integration.
