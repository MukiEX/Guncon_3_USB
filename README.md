## Credits / license

GunCon 3 protocol and decode tables from [beardypig/guncon3](https://github.com/beardypig/guncon3)
and [gameotaku79/Guncon3Windows](https://github.com/gameotaku79/Guncon3Windows) (both GPL-2.0),
so this project is GPL-2.0 too. Networking glue adapted from TinyUSB's `net_lwip_webserver`
example (MIT); USB host via [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB).

# GC3-XInput — GunCon 3 → DualShock 4 / XInput adapter for RP2040 / RP2350

Turns up to four Namco GunCon 3 light guns into XInput gamepads whose **left stick is the
absolute aim position**, for MAME-style "joystick as light gun" setups. Calibration runs in
a web page served by the adapter itself (like GP2040-CE), fullscreen on the monitor you play on.

Target board: **Waveshare RP2350-USB-A** (USB-A host port on GP12/GP13 via PIO-USB, WS2812 on GP16).
Plain Pico / Pico 2 builds use GP0 (D+) / GP1 (D−) for the host port.

```
 GunCon 3 ─┐
 GunCon 3 ─┤  USB hub ── USB-A (PIO-USB host, core1) ── RP2350 ── USB-C (native, core0) ── PC
 GunCon 3 ─┤                                                        • 1–4 XInput pads
 GunCon 3 ─┘                                                        • or: config web page
```

## Flashing

Hold BOOT, plug the board in, copy the matching `.uf2` onto the drive:

| File | Board |
|---|---|
| `gc3-xinput-rp2350-usb-a.uf2` | Waveshare RP2350-USB-A |
| `gc3-xinput-pico2.uf2` | Pico 2 / RP2350 boards (host on GP0/GP1, no LED) |
| `gc3-xinput-pico.uf2` | Pico / RP2040 boards (host on GP0/GP1, no LED) |

Later updates: System tab → *Firmware update (BOOTSEL)*.

## Two modes

| | Controller mode (default) | Config mode |
|---|---|---|
| PC sees | 1–4 XInput pads | USB network adapter (RNDIS / CDC-ECM) |
| Open | — | http://192.168.5.1 |
| LED | green = gun(s) connected, breathing amber = none | breathing blue |
| Enter | power-up, or *Restart as controllers* | hold **C1 + C2** on a gun for 3 s, or hold **BOOT** 1 s |

Holding BOOT for 1 s toggles between the two modes at any time.

## Calibration

1. Enter config mode, open http://192.168.5.1 on the PC whose monitor you'll play on.
2. Guns tab → **Calibrate** on a player. The page goes fullscreen (needed so the targets
   land on the real screen edges; the monitor's aspect ratio is recorded at the same time).
3. Shoot each target (5-point: corners + centre, or 9-point grid). Backspace steps back.
4. Check screen: a live crosshair follows the gun and trigger pulls leave marks.
   **C2** on the gun (or Enter) saves, **C1** (or R) redoes.

The fit is a full perspective (homography) transform, least-squares over all points, so a
gun off to the side or a slightly keystoned sensor bar still maps corner-to-corner. The raw
point for each target is averaged from the reports *just before* the trigger pull, so the
trigger jerk doesn't skew it.

Calibrations are stored per player slot, and a gun on hub port *N* always takes player *N*,
so keep each gun on the same hub port.

**Fullscreen test** draws every gun's crosshair exactly where its stick output lands, inside
the current game area.

## Controller type (Settings → Output)

* **DualShock 4** (default): each gun appears as a USB DS4 ("Wireless Controller", 054C:09CC).
  Windows treats it as a plain HID game controller, so no Xbox Game Bar / UI-navigation hijacking.
  DS4 sticks are 8-bit (256 steps across the screen), so the aim is **also** sent as two 16-bit
  axes, *Slider* (X) and *Dial* (Y), in bytes the real DS4 leaves as padding. In MAME, bind
  Lightgun X/Y to those for full precision. Buttons follow the Buttons-tab mapping, translated
  A→Cross, B→Circle, X→Square, Y→Triangle, LB/RB→L1/R1, LT/RT→L2/R2, Back→Share, Start→Options,
  Guide→PS. Feature reports (calibration, definition, pairing, firmware) are answered so Steam,
  DS4Windows and Linux `hid-playstation` accept it; each pad reports its own MAC. Note: on Linux,
  `hid-playstation` ignores the descriptor, so there you only get the 8-bit sticks.
* **Xbox 360 (XInput)**: 16-bit sticks everywhere, but subject to Windows' Xbox controller handling.

## Player numbers

Players tab, two modes:

* **In the order guns are plugged in** (default): first gun = player 1, and so on. A gun keeps its
  number until it's unplugged.
* **Fixed by hub port**: tie each player number to a port. Ports are identified by their path from the
  adapter (e.g. "hub port 2", or "hub port 1 → port 3" through nested hubs), so hub addresses changing
  between plug-ins doesn't matter. Pull a gun's trigger to light up its row and card before choosing.

Every GunCon 3 contains its own 1-port hub (`0C12:8801`); the port path is taken from where that
built-in hub is plugged in. Calibration belongs to the player number.

## Aspect / game area

MAME stretches the full joystick range over the *game's* screen area. If a 4:3 game is
pillarboxed on a 16:9 monitor, the gun has to be remapped into the centred 4:3 box:

* Modes: Full, 4:3, 3:4 (vertical), 5:4, Custom (any rectangle, in %).
* **C1 + C2 + Trigger** on any gun steps through the modes ticked in Settings; the LED blinks
  the mode number (1 = Full, 2 = 4:3, …). The power-up default is set in Settings.
* Off-screen (beyond the game area + margin): clamp to edge, hold last position, or jump to a corner.

## Off-screen reload (Settings → Aim)

Like Sinden / GUN4IR guns, the adapter can press a button for you when the gun is off-screen
(past the game area + margin, or pointed away so the gun sees no IR):

* **Tap when the aim leaves the screen**: one short press (60 ms by default, adjustable) each time
  the aim exits. It re-arms after the aim has been back on-screen for 120 ms, so jitter at the edge
  doesn't machine-gun it.
* **Trigger pulled off-screen**: that trigger press becomes the reload button instead of the trigger
  (decided when you pull, kept until you release).
* Or both. Off by default; never sent while aim is parked.

The reload button defaults to **B** (MAME's default *Button 2*); bind the game's reload action to it.
MAME's own `offscreen_reload` option only works for real lightgun/mouse devices, not for aim on a
joystick, so it doesn't help here. The page warns if a gun button is also mapped to the reload button (by default
A1 → B), since that button would then reload too; remap it on the Buttons tab if that matters.

## Default mapping (editable on the Buttons tab)

| GunCon 3 | XInput |
|---|---|
| Aim | Left stick (absolute) |
| Trigger | A |
| A1 / A2 | B / X |
| B1 / B2 | Y / LB |
| C1 / C2 | Back / Start |
| Stick A | Right stick |
| Stick B | D-pad |
| Stick A / B click | LS / RS |

C1 and C2 reach the PC ~60 ms late (the adapter waits to see whether they're the start of a
C1+C2 hotkey); quick taps are still delivered. Hotkeys can be turned off in Settings.

## MAME notes

* Map *Lightgun X / Y* (per player) to the left stick axes of that player's pad.
* Turn off joystick shaping or the aim gets bent: in `mame.ini` set `joystick_deadzone 0.0`
  and `joystick_saturation 1.0` (frontends/cores have equivalent options).

## Things to verify on real hardware

These couldn't be checked without the hardware and are worth confirming first:

* **Windows XInput binding.** The device is a composite with one Xbox 360-class interface per
  pad and an MS OS 1.0 `XUSB10` compatible ID on each, so `xusb22` binds per interface. It uses
  VID 045E with an unused PID (0x7C33) so Linux `xpad` also binds. Windows caches MS OS
  descriptors per VID/PID/bcdDevice; bcdDevice changes with the pad count, but if you edit
  descriptors, bump it or delete the device's `usbflags` registry entry.
* **IR indicator bits** (`ind ●●` on the gun cards): decoded but their meaning is unknown, so
  off-screen detection is position-based only.
* **Stick B → D-pad direction**: assumes low Y = up. If it's flipped, say so and it's one line.
* **Power:** four guns through an unpowered hub off the board's 5 V may be marginal; a powered
  hub is the safe choice.

## Troubleshooting config mode

* **Adapter gets 192.168.5.2 but the page/ping fails:** a VPN is capturing the traffic. With a
  Tailscale exit node active, Tailscale blocks local subnets unless *Allow local network access*
  is enabled in its exit-node menu.
* **Browser fails instantly:** it may be forcing HTTPS. Type `http://192.168.5.1/` explicitly.

## Building

Needs Pico SDK 2.x (with TinyUSB and lwIP submodules), `arm-none-eabi-gcc`, CMake, Ninja, Python 3.

```sh
# lib/Pico-PIO-USB is fetched by build.sh if missing
export PICO_SDK_PATH=/path/to/pico-sdk
./build.sh            # -> dist/*.uf2
```

Board options: `-DPICO_BOARD=...`, `-DGC3_PIO_USB_DP_PIN=n` (D− is always D+ + 1).
Other knobs are in `src/config.h`.

### Source map

| File | What |
|---|---|
| `src/guncon3.c`, `guncon3_decode.c` | TinyUSB host class driver for the GunCon 3: sends the key, decodes reports, slot-per-hub-port, trigger-edge capture |
| `src/mapper.c` | homography, aspect area, smoothing, off-screen, hotkeys, XInput report |
| `src/xinput_dev.c`, `usb_descriptors.c` | multi-pad XInput device driver, MS OS descriptors, RNDIS/ECM descriptors |
| `src/http.c`, `net.c` | lwIP raw-API HTTP server + JSON API, DHCP/DNS |
| `src/settings.c` | settings + calibrations in the last flash sector |
| `web/index.html` | the config page (gzipped into the firmware at build time) |
| `test/` | decoder fuzz test vs. the reference, homography test, mock API server for the page |

Tests: `test/run_tests.sh` (decoder fuzz vs. the C# reference, homography solver, off-screen reload + settings migration); `sudo PICO_SDK_PATH=… test/http_sim/run.sh` (the real HTTP/settings code over lwIP on a TAP device: validates every JSON endpoint and a save round-trip); and
`node test/mock_server.mjs` then open http://localhost:8077.
