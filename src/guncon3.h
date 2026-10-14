// GunCon 3 USB host driver (TinyUSB application class driver)
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Button bits (raw gun buttons, our own numbering)
enum {
  GB_TRIGGER = 0,
  GB_A1, GB_A2,
  GB_B1, GB_B2,
  GB_C1, GB_C2,
  GB_ACLICK, GB_BCLICK,
  GB_COUNT
};

typedef struct {
  bool     connected;
  uint8_t  hub_port;      // 0 = directly on the root port
  uint16_t buttons;       // bitmask of GB_*
  int16_t  x, y;          // raw aim
  int16_t  z;             // raw "Z" (distance-ish, informational)
  uint8_t  ax, ay;        // stick A (0..255, ~128 centre)
  uint8_t  bx, by;        // stick B
  uint8_t  ind;           // bit0 = indicator 1, bit1 = indicator 2 (IR status, informational)
  uint32_t reports;       // good reports received
  uint32_t bad;           // checksum failures
  // Trigger-edge capture for calibration: raw aim averaged over the
  // reports just *before* the trigger was pulled (avoids trigger jerk)
  uint32_t shot_seq;
  int16_t  shot_x, shot_y;
} gun_raw_t;

// USB host diagnostics
#define USB_DIAG_MAX 8
typedef struct {
  uint8_t  addr, hub_addr, hub_port, speed;   // speed: 0 full, 1 low, 2 high (TinyUSB tusb_speed_t)
  uint16_t vid, pid;
} usb_dev_info_t;
int guncon3_usb_list(usb_dev_info_t *out, uint32_t *events);

// Core0, before launching core1
void guncon3_init_shared(void);

// Core1: register and run the host stack
void guncon3_host_core1_entry(void);

// Core0: copy a consistent snapshot of a slot
void guncon3_get(int slot, gun_raw_t *out);

// Pure decode (exposed for tests): data[15] -> out[13] in wire order. false on checksum fail.
bool guncon3_decode(const uint8_t data[15], const uint8_t key[8], uint8_t out[13]);
extern const uint8_t guncon3_key[8];
