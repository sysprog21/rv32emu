# VirtIO sound in rv32emu

This document explains how to build, run, and verify the virtio-snd
playback device in rv32emu.

The current implementation provides PCM playback through the VirtIO sound
control and TX queues. Event and capture (RX) queues are not supported.

## Host dependency

virtio-snd uses PortAudio as the host playback backend.

On Debian/Ubuntu:

```shell
sudo apt install portaudio19-dev
```

On macOS with Homebrew:

```shell
brew install portaudio
```

## Build

Build rv32emu with system emulation and virtio-snd enabled:

```shell
make system_defconfig
```
Or can use Kconfig to choose virtio-snd:
```shell
make config
```

The virtio-snd build option is only available with system emulation. Builds
without virtio-snd do not require PortAudio.

## Run

Start system emulation with the sound device enabled:

```shell
build/rv32emu \
  -k build/linux-image/Image \
  -i build/linux-image/rootfs.cpio \
  -x vsnd
```

The generated device tree exposes the sound device through VirtIO-MMIO.
After Linux boots, the device should bind to the `virtio_snd` driver.

## Guest verification

Check the bound VirtIO driver:

```shell
readlink /sys/bus/virtio/devices/virtio0/driver
```

The output should end with:

```text
bus/virtio/drivers/virtio_snd
```

Check the ALSA card:

```shell
cat /proc/asound/cards
```

A detected device should contain:

```text
VirtIO SoundCard
```

Check the playback PCM device:

```shell
aplay -l
```

A working configuration should report a VirtIO playback device similar to:

```text
card 0: SoundCard [VirtIO SoundCard], device 0: virtio-snd [VirtIO PCM 0]
```

## Playback test

`speaker-test` can exercise the actual PCM path without requiring a test audio
file:

```shell
speaker-test -D hw:0,0 -c 1 -r 48000 -F S16_LE -t sine -l 2
```

This configuration generates a mono sine wave at 48 kHz using 16-bit
little-endian samples and runs two playback loops.

A successful run prints playback progress such as:

```text
0 - Mono
Time per period = ...
```

and returns to the shell after the requested loops complete.

The playback path exercised by this test is:

```text
guest PCM -> TX thread -> pending PCM queue -> PortAudio callback -> speaker
                                                   |
                                                   v
used ring <- completion thread <- completed queue
```

A TX request remains in flight until the corresponding PCM data has been
consumed. The PortAudio callback then moves the request to the completed queue,
and the completion thread updates the used ring and raises the VirtIO
interrupt.

When the PortAudio callback requests data before a PCM buffer is available,
the output buffer is zero-filled so the host plays silence instead of stale or
invalid PCM data.

## Additional Cool Test
Play music by `virtio-blk` and `virtio-snd`

Host side transfer mp3 to wav:
```shell
ffmpeg -i song.mp3 -ac 1 -ar 48000 -sample_fmt s16 song.wav
```
In rv32emu folder
```shell
dd if=/dev/zero of=build/disks/song.img bs=1M count=128

mkdir -p /tmp/rv32-songdisk
sudo mount -o loop build/disks/song.img /tmp/rv32-songdisk
sudo cp <path to song.wav> /tmp/rv32-songdisk/song.wav
sync
sudo umount /tmp/rv32-songdisk

build/rv32emu \
  -k build/linux-image/Image \
  -i build/linux-image/rootfs.cpio \
  -x vsnd \
  -x vblk:build/disks/song.img
```

Guest side
1. mount virtio-blk
```shell
mkdir -p /mnt/songdisk
mount /dev/vda /mnt/songdisk
```
result
```
# mkdir -p /mnt/songdisk
# mount /dev/vda /mnt/songdisk
[  190.094263] EXT4-fs (vda): mounted filesystem with ordered data mode. Quota mode: disabled.
```

2. play music and check whether stop in half
```shell
aplay -D hw:0,0 --period-size=12000 --buffer-size=48000 /mnt/songdisk/song.wav
```
result
```
# aplay -D hw:0,0 --period-size=12000 --buffer-size=48000 /mnt/songdisk/song.wav
Playing WAVE '/mnt/songdisk/song.wav' : Signed 16 bit Little Endian, Rate 48000 Hz, Mono
```

## CI

The virtio-snd CI test is implemented in:

```text
.ci/sound.sh
```

It verifies:

1. Linux boots and the guest can log in.
2. The VirtIO device binds to the `virtio_snd` driver.
3. ALSA enumerates the VirtIO SoundCard.
4. `aplay -l` enumerates the playback PCM device.
5. `speaker-test` completes two mono playback loops using the known working
   48 kHz S16_LE configuration.

The playback test uses a bounded loop count instead of terminating
`speaker-test` with an external timeout so slower emulator or CI environments
do not fail merely because PCM playback takes longer than native execution.

## Future Work
1. Implement RX queue
2. Event-driven wakeup
