#include <string.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"

#include "settings.h"
#include "diag.h"

#define SETTINGS_MAGIC   0x47433358u   // 'GC3X'
#define SETTINGS_VERSION 1
#define SETTINGS_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

settings_t g_settings;

static uint32_t crc32(const void *data, size_t len) {
  const uint8_t *p = data;
  uint32_t crc = 0xFFFFFFFFu;
  while (len--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

void settings_defaults(settings_t *s) {
  memset(s, 0, sizeof(*s));
  s->magic = SETTINGS_MAGIC;
  s->version = SETTINGS_VERSION;
  s->size = sizeof(*s);
  s->pad_count = 4;
  s->aspect_default = ASPECT_FULL;
  s->aspect_cycle_mask = (1u << ASPECT_FULL) | (1u << ASPECT_4_3);
  s->offscreen_mode = OFF_CLAMP;
  s->offscreen_margin = 8;
  s->smoothing = 2;
  s->stick_a_dest = DEST_RSTICK;
  s->stick_b_dest = DEST_DPAD;
  s->dpad_deadzone = 40;
  s->hotkeys = 1;
  // Defaults follow MAME's usual joystick layout: A = Button 1, B = Button 2, Back = Coin
  s->button_map[GB_TRIGGER] = XI_A;
  s->button_map[GB_A1]      = XI_B;
  s->button_map[GB_A2]      = XI_X;
  s->button_map[GB_B1]      = XI_Y;
  s->button_map[GB_B2]      = XI_LB;
  s->button_map[GB_C1]      = XI_BACK;
  s->button_map[GB_C2]      = XI_START;
  s->button_map[GB_ACLICK]  = XI_LS;
  s->button_map[GB_BCLICK]  = XI_RS;
  s->monitor_aspect = 16.0f / 9.0f;
  s->custom_rect[0] = 0.125f; s->custom_rect[1] = 0.0f;
  s->custom_rect[2] = 0.875f; s->custom_rect[3] = 1.0f;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settings_sanitize(settings_t *s) {
  if (s->pad_count < 1 || s->pad_count > MAX_GUNS) s->pad_count = MAX_GUNS;
  if (s->aspect_default >= ASPECT_COUNT) s->aspect_default = ASPECT_FULL;
  s->aspect_cycle_mask &= (1u << ASPECT_COUNT) - 1;
  if (!s->aspect_cycle_mask) s->aspect_cycle_mask = 1u << ASPECT_FULL;
  if (s->offscreen_mode >= OFF_COUNT) s->offscreen_mode = OFF_CLAMP;
  if (s->offscreen_margin > 50) s->offscreen_margin = 50;
  if (s->smoothing > 9) s->smoothing = 9;
  if (s->stick_a_dest >= DEST_COUNT) s->stick_a_dest = DEST_NONE;
  if (s->stick_b_dest >= DEST_COUNT) s->stick_b_dest = DEST_NONE;
  if (s->dpad_deadzone > 127) s->dpad_deadzone = 127;
  for (int i = 0; i < GB_COUNT; i++) if (s->button_map[i] >= XI_COUNT) s->button_map[i] = XI_NONE;
  if (!(s->monitor_aspect > 0.2f && s->monitor_aspect < 5.0f)) s->monitor_aspect = 16.0f / 9.0f;
  for (int i = 0; i < 4; i++) s->custom_rect[i] = clampf(s->custom_rect[i], 0.0f, 1.0f);
  if (s->custom_rect[2] - s->custom_rect[0] < 0.05f || s->custom_rect[3] - s->custom_rect[1] < 0.05f) {
    s->custom_rect[0] = 0; s->custom_rect[1] = 0; s->custom_rect[2] = 1; s->custom_rect[3] = 1;
  }
  for (int g = 0; g < MAX_GUNS; g++) {
    for (int k = 0; k < 8; k++) {
      float v = s->calib[g].h[k];
      if (v != v || v > 1e9f || v < -1e9f) s->calib[g].valid = 0;   // NaN / garbage
    }
  }
}

void settings_load(void) {
  const settings_t *f = (const settings_t *)(XIP_BASE + SETTINGS_OFFSET);
  if (f->magic == SETTINGS_MAGIC && f->version == SETTINGS_VERSION && f->size == sizeof(settings_t) &&
      f->crc == crc32(f, offsetof(settings_t, crc))) {
    memcpy(&g_settings, f, sizeof(g_settings));
  } else {
    settings_defaults(&g_settings);
  }
  settings_sanitize(&g_settings);
}

static uint8_t s_page[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

static void do_flash_write(void *param) {
  (void) param;
  flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(SETTINGS_OFFSET, s_page, (sizeof(settings_t) + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1));
}

bool settings_save(void) {
  g_settings.magic = SETTINGS_MAGIC;
  g_settings.version = SETTINGS_VERSION;
  g_settings.size = sizeof(settings_t);
  g_settings.crc = crc32(&g_settings, offsetof(settings_t, crc));
  memset(s_page, 0xFF, sizeof(s_page));
  memcpy(s_page, &g_settings, sizeof(g_settings));
  return flash_safe_execute(do_flash_write, NULL, 500) == PICO_OK;
}

#define BOOT_MAGIC 0x6C33B007u

int boot_mode_requested(void) {
  int mode = GC3_DEFAULT_BOOT_WEB ? BOOT_WEB : BOOT_XINPUT;
  if (watchdog_hw->scratch[0] == BOOT_MAGIC) mode = (int) watchdog_hw->scratch[1];
  watchdog_hw->scratch[0] = 0;
  return mode;
}

void boot_mode_arm(int mode) {
  // Any later watchdog/crash reboot comes back in the same mode (power-on clears this)
  watchdog_hw->scratch[0] = BOOT_MAGIC;
  watchdog_hw->scratch[1] = (uint32_t) mode;
}

void reboot_into(int mode) {
  diag_note_reboot_requested();
  watchdog_hw->scratch[0] = BOOT_MAGIC;
  watchdog_hw->scratch[1] = (uint32_t) mode;
  watchdog_reboot(0, 0, 10);
  while (1) tight_loop_contents();
}

void reboot_bootsel(void) {
  diag_note_reboot_requested();
  rom_reset_usb_boot_extra(-1, 0, false);
}

const char *aspect_name(int a) {
  switch (a) {
    case ASPECT_FULL: return "Full";
    case ASPECT_4_3: return "4:3";
    case ASPECT_3_4: return "3:4";
    case ASPECT_5_4: return "5:4";
    case ASPECT_CUSTOM: return "Custom";
    default: return "?";
  }
}
