# Who did what

## mukisign (project owner)
- The idea, requirements and every design decision: hub of up to four GunCon 3s, absolute
  aim on the left stick for MAME, corner-to-corner calibration, aspect toggle, GP2040-style
  web configurator, two-build layout, player numbering modes, DS4 mode, aim park.
- All hardware testing on the Waveshare RP2350-USB-A, with screenshots and diagnostic logs.
  Every fix from 0.1.1 on started from one of these reports (see CHANGELOG.md, *Reported*).
- Found the Tailscale exit-node cause of the 0.1.1 network failure.

## Claude (Anthropic; configured model claude-opus-5-5)
Wrote all of the project's own code and docs, in response to the above:
- `src/` (all files), `web/index.html`, `tools/embed_web.py`, `CMakeLists.txt`, `build.sh`
- `test/` (decoder fuzz test, homography test, mock API server, `http_sim` harness)
- `README.md`, `CHANGELOG.md`, this file
- See CHANGELOG.md for what changed in each release and why
- Every commit carries a `Co-Authored-By: Claude` trailer.

Claude did not have the hardware; everything marked "verify on real hardware" in the
README is unconfirmed until tested.

## Third-party code (neither of the above)
| Path / part | Source | License |
|---|---|---|
| `lib/tinyusb/` | TinyUSB 0.21.0 (hathach), vendored from 0.1.2 | MIT |
| `lib/Pico-PIO-USB/` | sekigon-gonnoc/Pico-PIO-USB @ 5a37a66 | MIT |
| `patches/pico-pio-usb-hub-hotplug.patch` | GP2040-CE `fix-rx-enable` (Pico-PIO-USB issue #149) | MIT |
| `patches/tinyusb-0.21-ctrl-timeout.patch` | GP2040-CE TinyUSB control-transfer timeout | MIT |
| `src/guncon3_decode.c` key table + algorithm | ported from gameotaku79/Guncon3Windows and beardypig/guncon3 | GPL-2.0 |
| DS4 descriptor / feature-report layout in `src/ds4.c` | after GP2040-CE | MIT |
| lwIP glue in `src/net.c` | adapted from TinyUSB `net_lwip_webserver` example | MIT |
| `pico_sdk_import.cmake` | Pico SDK | BSD-3 |
| lwIP, Pico SDK | used from the SDK, not vendored | BSD-3 |

Because the decoder derives from GPL-2.0 code, the project as a whole is GPL-2.0 (`LICENSE`).

## How this history was made
The repository was assembled after the fact from the source zip handed over at each release
(0.1.0 … 0.2.2): one commit and one annotated tag per release, dated when that zip was made.
`lib/` is committed exactly as shipped (already patched from 0.1.6 on), so every tag builds
offline with `./build.sh`. Intermediate edits between releases weren't kept, so each commit
is the whole step from one release to the next.
