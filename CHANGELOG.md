# Changelog

One entry per release. *Reported* = what was found on real hardware or asked for
(by the project owner); *Changes* = the code written in response (by Claude).

## 0.2.2 — Fix broken settings JSON, BOOT button, HTTP hang; XInput default; aim park

**Reported / requested**

User reported: button assignments blank, only one gun in Xbox mode,
holding BOOT did nothing. Asked for XInput as default again and a way to
temporarily turn off the left stick.

**Changes**

- http.c: new settings fields were emitted inside "map":[...] since
  0.1.9, so the page failed to parse settings and could save Pads=1 and an
  empty map (root cause of the first two reports)
- BOOTSEL read used CS_PIN_INDEX 2 on RP2350; correct is 1
- HTTP: a Connection: close request whose POST body arrived split hung
- Default output XInput again (DS4 remains selectable)
- Aim park: C1+C2+A1, a page button, or an at-power-up setting centres all
  sticks so the guns don't drive Windows/Steam
- test/http_sim: runs the real http.c/settings.c/net.c over lwIP on a TAP
  device and validates every JSON endpoint and a save round-trip

## 0.2.1 — Plug-order mode exposes only connected guns

**Reported / requested**

User asked that in plug-order mode only plugged-in controllers appear.

**Changes**

- Pad count follows connected guns; re-enumerates via tud_disconnect /
  tud_connect, debounced (400 ms; 1200 ms for the first connect)
- bcdDevice tracks pad count so Windows re-reads descriptors

## 0.2.0 — DualShock 4 output mode

**Reported / requested**

User asked for a DS4 mode (as default) to keep Windows' Xbox UI handling
out of the way.

**Changes**

- HID DS4 (054C:09CC) per pad; feature reports 0x02/0x03/0x12/0xA3
  answered with a unique MAC per pad (report tables after GP2040-CE, MIT)
- Extra 16-bit Slider/Dial axes carry full-precision aim in bytes 48-51
- Controller type selectable in Settings

## 0.1.9 — Players tab: plug-order or fixed-by-port numbering; settings v2

**Reported / requested**

Port-based numbering misbehaved with both of the user's hubs. User asked
for plug-order mode plus a port mode with a tab to assign ports, with
trigger-pull highlighting.

**Changes**

- Player mode: plug order (default) or fixed by port path
- Players tab with port paths, seat selectors, trigger-pull highlight
- settings v2 (player_mode, port_map) with v1 migration

## 0.1.8 — Root-port liveness probe; GunCon built-in hub awareness; aim rate

**Reported / requested**

Unplugging the hub left guns showing (NO DATA) and replugging did nothing.
User noticed tracking felt slow with one gun.

**Changes**

- GET_STATUS probe on the root device every 500 ms; two failures force a
  root disconnect in PIO-USB so TinyUSB tears down the tree
- Each GunCon 3 contains its own 1-port hub (0C12:8801); player port is
  taken from where that hub plugs in
- CFG_TUH_HUB 8
- Gun cards show report rate and aim-update rate

## 0.1.7 — Root-port enumeration retry

**Reported / requested**

Hub plugged in after power-up sometimes never enumerated.

**Changes**

- If the root port shows a device but nothing enumerated, fake a
  detach/attach after 2, 4 and 8 s

## 0.1.6 — Apply GP2040-CE hub hot-plug fixes; live status; two builds

**Reported / requested**

Unplugged guns never disappeared and replugged ones never came back; the
page needed real-time status; user asked for only two builds
(normal + boots-to-configurator).

**Changes**

- patches/pico-pio-usb-hub-hotplug.patch: GP2040-CE's fix-rx-enable for
  Pico-PIO-USB (RX SM enabled after TX), pinned to 5a37a66
- patches/tinyusb-0.21-ctrl-timeout.patch: control-transfer timeout (GP2040-CE)
- Live polling of gun state on the page, staleness ("NO DATA") markers
- build.sh produces gc3-xinput-rp2350-usb-a.uf2 and -config.uf2 only

## 0.1.5 — Filter per-transfer TinyUSB log spam

**Reported / requested**

Diag log filled with 'OK' lines.

**Changes**

- Drop per-transfer lines (on EP / Queue EP / Claimed / Released /
  xfer callback / OK) from the diagnostic ring

## 0.1.4 — Watchdog, core1 heartbeat, persistent crash/diagnostic log

**Reported / requested**

Adapter became unresponsive to all devices after anything was unplugged.
User asked that any GunCon (not only P1) can open the configurator and that
BOOT works with no guns attached.

**Changes**

- Hardware watchdog (3 s) + core1 heartbeat supervision (2 s)
- HardFault / panic capture in uninitialised RAM, reset-reason classification
- TinyUSB log captured into a ring buffer shown on the System tab
- Boot mode is re-armed in watchdog scratch, so a self-reset comes back in
  the same mode; BOOT-button reads that fail (core1 not pausing) are logged
- System tab: Diagnostics panel (last reset, fault PC/LR, logs, Copy all)
- Note: C1+C2 on any gun already opened the configurator since 0.1.0

## 0.1.3 — More hubs/devices; treat all-zero reports as no IR

**Reported / requested**

User's dock contains several chained hubs; only one of two guns found.

**Changes**

- Raise CFG_TUH_HUB / CFG_TUH_DEVICE_MAX
- All-zero x/y/z reports (no IR seen) are treated as off-screen

## 0.1.2 — Switch to TinyUSB 0.21 for hub support; USB device list; LED colour fix

**Reported / requested**

Guns not detected when plugged through a hub (only worked if hub + guns
were connected before plugging in the hub); LED showed red for green.

**Changes**

- Vendor TinyUSB 0.21.0 (lib/tinyusb) instead of the SDK's copy for its
  much better hub handling; port to the 0.21 API (driver open returns
  bytes consumed, tuh_bus_info_get, usbd_edpt_xfer is_isr argument)
- System tab: list of enumerated USB devices
- WS2812 on this board is R,G,B order

## 0.1.1 — Fix config-mode network stall; move to 192.168.5.1

**Reported / requested**

Page and ping failed in config mode (LED blue). User asked to move the
subnet to 192.168.5.1 to avoid clashing with other USB-network projects.
User found the remaining failure was their Tailscale exit node.

**Changes**

- net.c: tud_network_recv_cb returned true for frames it had dropped,
  stalling the RX path; now returns false when the frame wasn't kept
- linkoutput waits at most 50 ms instead of blocking
- Address 192.168.5.1, DHCP leases .2-.4
- README: VPN / Tailscale exit-node troubleshooting note

## 0.1.0 — Initial GunCon 3 -> XInput adapter firmware

**Reported / requested**

Requested by the user: up to 4 GunCon 3s through a USB hub on a Waveshare
RP2350-USB-A, each exposed as an XInput pad with absolute aim on the left
stick (MAME "absolute joystick positioning"); calibration so screen corners
map to stick corners; 4:3 aspect toggle; GP2040-style built-in web
configurator with fullscreen calibration.

**Changes**

- TinyUSB host class driver for the GunCon 3 (0B9A:0800) on PIO-USB (core1):
  sends the 8-byte key, decodes the 15-byte obfuscated reports (decoder
  ported from Guncon3Windows / beardypig, fuzz-tested against the C#
  reference: 2M reports, 0 mismatches)
- Multi-pad XInput device (one 0xFF/0x5D/0x01 interface per pad, MS OS 1.0
  XUSB10 compat ID, VID 045E PID 7C33)
- Mapper: homography calibration, aspect modes (Full/4:3/3:4/5:4/Custom),
  smoothing, off-screen handling, C1+C2 hotkeys
- Web config mode: RNDIS/CDC-ECM + lwIP, raw-API HTTP server, DHCP/DNS,
  embedded single-page configurator with fullscreen 5/9-point calibration
- Settings + calibrations in the last flash sector with CRC
- Tests: decoder fuzz, homography solver, mock API server for the page
