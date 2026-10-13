#!/bin/sh
# Build all targets. Needs PICO_SDK_PATH and arm-none-eabi-gcc on PATH.
set -e
cd "$(dirname "$0")"
[ -f lib/Pico-PIO-USB/src/pio_usb.c ] || git clone --depth 1 https://github.com/sekigon-gonnoc/Pico-PIO-USB lib/Pico-PIO-USB
mkdir -p dist
build() {  # name board [extra cmake args]
  name=$1; board=$2; shift 2
  cmake -S . -B build-$name -G Ninja -DPICO_BOARD=$board -DCMAKE_BUILD_TYPE=Release "$@" >/dev/null
  cmake --build build-$name
  cp build-$name/gc3_xinput.uf2 dist/gc3-xinput-$name.uf2
}
build rp2350-usb-a waveshare_rp2350_usb_a
build pico2 pico2
build pico pico
ls -l dist
