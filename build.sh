#!/bin/sh
# Build all targets. Needs PICO_SDK_PATH and arm-none-eabi-gcc on PATH.
set -e
cd "$(dirname "$0")"
# lib/ ships already patched; when fetching fresh copies, apply patches/ (GP2040-CE's hub hot-plug
# fix for Pico-PIO-USB and control-transfer timeout for TinyUSB 0.21)
if [ ! -f lib/tinyusb/src/tusb.c ]; then
  git clone --depth 1 -b 0.21.0 https://github.com/hathach/tinyusb lib/tinyusb
  (cd lib/tinyusb && patch -p1 < ../../patches/tinyusb-0.21-ctrl-timeout.patch)
fi
if [ ! -f lib/Pico-PIO-USB/src/pio_usb.c ]; then
  git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB lib/Pico-PIO-USB
  (cd lib/Pico-PIO-USB && git checkout 5a37a66 && patch -p1 < ../../patches/pico-pio-usb-hub-hotplug.patch)
fi
mkdir -p dist
build() {  # name board [extra cmake args]
  name=$1; board=$2; shift 2
  cmake -S . -B build-$name -G Ninja -DPICO_BOARD=$board -DCMAKE_BUILD_TYPE=Release "$@" >/dev/null
  cmake --build build-$name
  cp build-$name/gc3_xinput.uf2 dist/gc3-xinput-$name.uf2
}
build rp2350-usb-a waveshare_rp2350_usb_a
build rp2350-usb-a-config waveshare_rp2350_usb_a -DGC3_DEFAULT_BOOT_WEB=1
# Other boards (untested for now):
# build pico2 pico2
# build pico pico
ls -l dist
