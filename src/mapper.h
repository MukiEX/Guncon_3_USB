// Gun -> XInput mapping: calibration, aspect area, smoothing, off-screen, hotkeys
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "guncon3.h"

typedef struct __attribute__((packed)) {
  uint8_t  msg_type;   // 0x00
  uint8_t  length;     // 0x14
  uint16_t buttons;
  uint8_t  lt, rt;
  int16_t  lx, ly, rx, ry;
  uint8_t  reserved[6];
} xinput_report_t;

typedef struct {
  bool     connected;
  bool     calibrated;
  bool     offscreen;
  float    u, v;        // calibrated position on the whole monitor (0..1, may exceed)
  float    gx, gy;      // position inside the active game area (0..1, clamped as output)
  xinput_report_t rep;
} pad_out_t;

void mapper_init(void);
// Update all pads from the latest gun states. now_ms from to_ms_since_boot.
void mapper_update(uint32_t now_ms);
const pad_out_t *mapper_pad(int i);

int  mapper_aspect(void);            // current (runtime) aspect mode
void mapper_set_aspect(int a);
void mapper_game_rect(int aspect, float r[4]);   // x0,y0,x1,y1 of the game area on the monitor

// True once a gun held C1+C2 for HOTKEY_WEBMODE_MS (cleared on read)
bool mapper_take_webmode_request(void);

// Homography helper (exposed for tests)
bool mapper_apply_h(const float h[8], float x, float y, float *u, float *v);
