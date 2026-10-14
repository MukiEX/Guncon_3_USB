# Changelog

One entry per release. *Reported* = what was found on real hardware or asked for
(by the project owner); *Changes* = the code written in response (by Claude).

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
