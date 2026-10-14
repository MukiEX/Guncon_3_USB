#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "guncon3.h"

enum { ASPECT_FULL = 0, ASPECT_4_3, ASPECT_3_4, ASPECT_5_4, ASPECT_CUSTOM, ASPECT_COUNT };
enum { OFF_CLAMP = 0, OFF_HOLD, OFF_CORNER, OFF_COUNT };
enum { DEST_NONE = 0, DEST_RSTICK, DEST_DPAD, DEST_COUNT };
enum { PLAYERS_BY_ORDER = 0, PLAYERS_BY_PORT, PLAYERS_COUNT };
enum { PAD_DS4 = 0, PAD_XINPUT, PAD_COUNT };   // controller type in controller mode
#define PORT_PATH_NONE 0xFFFF   // port_map entry: no port assigned

// XInput targets for the button map
enum {
  XI_NONE = 0, XI_A, XI_B, XI_X, XI_Y, XI_LB, XI_RB, XI_LT, XI_RT,
  XI_BACK, XI_START, XI_GUIDE, XI_LS, XI_RS,
  XI_DUP, XI_DDOWN, XI_DLEFT, XI_DRIGHT,
  XI_COUNT
};

typedef struct {
  uint8_t valid;
  uint8_t points;       // how many points the calibration used (info only)
  uint8_t _pad[2];
  float   h[8];         // homography raw(x,y) -> screen(u,v), normalized 0..1, h[8] == 1
} gun_calib_t;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t size;

  uint8_t  pad_count;            // XInput pads exposed (1..4)
  uint8_t  aspect_default;       // ASPECT_* at power-up
  uint8_t  aspect_cycle_mask;    // bit per ASPECT_* the hotkey cycles through
  uint8_t  offscreen_mode;       // OFF_*
  uint8_t  offscreen_margin;     // percent outside the game area that counts as off-screen
  uint8_t  smoothing;            // 0 (off) .. 9 (heavy)
  uint8_t  stick_a_dest;         // DEST_* for the front (A) thumbstick
  uint8_t  stick_b_dest;         // DEST_* for the rear (B) thumbstick
  uint8_t  dpad_deadzone;        // 0..127 raw units for stick -> d-pad
  uint8_t  hotkeys;              // C1+C2 hotkeys enabled
  uint8_t  button_map[GB_COUNT]; // XI_* per gun button
  uint8_t  aim_park_at_boot;     // 1 = start with aim output parked (stick centred)
  uint8_t  _pad[1];

  float    monitor_aspect;       // width / height of the screen used for calibration
  float    custom_rect[4];       // x0, y0, x1, y1 (normalized) for ASPECT_CUSTOM

  gun_calib_t calib[MAX_GUNS];

  // v2
  uint8_t  player_mode;          // PLAYERS_*
  uint8_t  pad_type;             // PAD_* (was padding in early v2 = 0 = DS4)
  uint16_t port_map[MAX_GUNS];   // player i <- gun on this port path (see guncon3.h)

  uint32_t crc;
} settings_t;

extern settings_t g_settings;

void settings_load(void);
void settings_defaults(settings_t *s);
bool settings_save(void);            // writes flash (pauses core1 briefly)
void settings_sanitize(settings_t *s);

// Boot mode handoff through watchdog scratch registers
enum { BOOT_XINPUT = 0, BOOT_WEB = 1 };
int  boot_mode_requested(void);
void reboot_into(int mode);
void boot_mode_arm(int mode);
void reboot_bootsel(void);

const char *aspect_name(int a);
