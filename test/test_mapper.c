// Host test for the off-screen reload logic in mapper.c and the v2 -> v3 settings migration.
// Built by run_tests.sh with the http_sim stubs standing in for the Pico SDK.
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "settings.h"
#include "mapper.h"
#include "guncon3.h"

uint8_t fake_flash[4096];
typedef struct { uint32_t scratch[8]; } wd_t; wd_t wdhw;
void flash_range_erase(uint32_t o, uint32_t n) { memset(fake_flash + o, 0xFF, n); }
void flash_range_program(uint32_t o, const uint8_t *d, uint32_t n) { memcpy(fake_flash + o, d, n); }
int flash_safe_execute(void (*f)(void *), void *p, unsigned t) { (void) t; f(p); return 0; }
void diag_note_reboot_requested(void) {}
void led_flash(int count, uint32_t rgb) { (void) count; (void) rgb; }

static gun_raw_t G;
void guncon3_get(int slot, gun_raw_t *out) {
  if (slot == 0) *out = G; else memset(out, 0, sizeof(*out));
}

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define BIT_A 0x1000
#define BIT_B 0x2000
static uint32_t now;

// Run `ms` of 1 ms updates with the gun at screen position u (0..1 on-screen; >1 off), y centred.
// Returns how many ms the B / A bits were set.
static void run(int ms, float u, uint16_t buttons, int *b_ms, int *a_ms) {
  *b_ms = *a_ms = 0;
  for (int k = 0; k < ms; k++) {
    G.connected = true;
    if (u < -5) { G.x = G.y = G.z = 0; }            // no IR
    else { G.x = (int16_t)((u - 0.5f) * 10000); G.y = 0; G.z = 300; }
    G.buttons = buttons;
    G.reports++;
    G.last_ms = now;
    mapper_update(now);
    uint16_t xb = mapper_pad(0)->rep.buttons;
    if (xb & BIT_B) (*b_ms)++;
    if (xb & BIT_A) (*a_ms)++;
    now++;
  }
}

static void setup(uint8_t flags) {
  settings_defaults(&g_settings);
  g_settings.smoothing = 0;
  g_settings.button_map[GB_A1] = XI_X;               // keep B free for the reload
  g_settings.reload_flags = flags;
  g_settings.reload_button = XI_B;
  g_settings.reload_tap_ms = 60;
  float h[8] = { 1e-4f, 0, 0.5f, 0, 1e-4f, 0.5f, 0, 0 };   // u = x/10000 + 0.5
  g_settings.calib[0].valid = 1;
  memcpy(g_settings.calib[0].h, h, sizeof(h));
  memset(&G, 0, sizeof(G));
  mapper_init();
  now = 1000;
  int b, a;
  G.connected = false; mapper_update(now++);            // disconnected pass resets the pad state
}

static void test_exit_tap(void) {
  int b, a;
  setup(RELOAD_ON_EXIT);
  run(200, 0.5f, 0, &b, &a);  CHECK(b == 0, "no tap while on-screen (%d)", b);
  run(200, 1.3f, 0, &b, &a);  CHECK(b == 60, "one 60 ms tap on leaving (%d)", b);
  run(50, 0.5f, 0, &b, &a);   CHECK(b == 0, "none coming back");
  run(100, 1.3f, 0, &b, &a);  CHECK(b == 0, "no tap after only 50 ms on-screen (edge jitter) (%d)", b);
  run(200, 0.5f, 0, &b, &a);
  run(100, -9.0f, 0, &b, &a); CHECK(b == 60, "pointing away (no IR) taps too (%d)", b);
  run(200, 0.5f, 0, &b, &a);
  run(100, 1.05f, 0, &b, &a); CHECK(b == 0, "inside the 8%% margin isn't off-screen (%d)", b);
  int tb, ta;
  run(100, 1.3f, 1u << GB_TRIGGER, &tb, &ta);
  CHECK(ta == 100, "exit-only mode leaves the trigger alone off-screen (%d)", ta);
}

static void test_connect_offscreen(void) {
  int b, a;
  setup(RELOAD_ON_EXIT);
  run(300, 1.3f, 0, &b, &a);  CHECK(b == 0, "no tap just for connecting while off-screen (%d)", b);
}

static void test_trigger(void) {
  int b, a;
  setup(RELOAD_ON_TRIGGER);
  run(200, 0.5f, 0, &b, &a);
  run(100, 0.5f, 1u << GB_TRIGGER, &b, &a);  CHECK(a == 100 && b == 0, "on-screen trigger = A (a%d b%d)", a, b);
  run(20, 0.5f, 0, &b, &a);
  run(100, 1.3f, 0, &b, &a);  CHECK(b == 0, "trigger-only mode: leaving doesn't tap (%d)", b);
  run(100, 1.3f, 1u << GB_TRIGGER, &b, &a);  CHECK(b == 100 && a == 0, "off-screen trigger = B (a%d b%d)", a, b);
  run(100, 0.5f, 1u << GB_TRIGGER, &b, &a);  CHECK(b == 100 && a == 0, "held press stays a reload after coming back (a%d b%d)", a, b);
  run(10, 0.5f, 0, &b, &a);
  run(50, 0.5f, 1u << GB_TRIGGER, &b, &a);   CHECK(a == 50 && b == 0, "next press on-screen fires again (a%d b%d)", a, b);
  CHECK(mapper_pad(0)->reloads == 1, "reload counter %lu", (unsigned long) mapper_pad(0)->reloads);
}

static void test_parked(void) {
  int b, a;
  setup(RELOAD_ON_EXIT | RELOAD_ON_TRIGGER);
  mapper_set_aim_parked(true);
  run(200, 0.5f, 0, &b, &a);
  run(100, 1.3f, 0, &b, &a);                 CHECK(b == 0, "no tap while parked (%d)", b);
  run(100, 1.3f, 1u << GB_TRIGGER, &b, &a);  CHECK(a == 100 && b == 0, "parked trigger stays A (a%d b%d)", a, b);
}

static void test_off(void) {
  int b, a;
  setup(0);
  run(200, 0.5f, 0, &b, &a);
  run(100, 1.3f, 1u << GB_TRIGGER, &b, &a);  CHECK(b == 0 && a == 100, "feature off: nothing changes (a%d b%d)", a, b);
}

// ---- settings v2 -> v3 ----
static uint32_t crc32(const void *data, size_t len) {
  const uint8_t *p = data; uint32_t crc = 0xFFFFFFFFu;
  while (len--) { crc ^= *p++; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1))); }
  return ~crc;
}

static void test_migration(void) {
  settings_t v2;
  settings_defaults(&v2);
  size_t v2data = offsetof(settings_t, reload_flags), v2size = ((v2data + 3) & ~(size_t) 3) + 4;
  v2.version = 2; v2.size = (uint16_t) v2size;
  v2.pad_count = 3; v2.smoothing = 5; v2.button_map[GB_A1] = XI_Y; v2.pad_type = PAD_DS4;
  v2.calib[1].valid = 1; v2.calib[1].h[0] = 0.25f; v2.port_map[2] = 0x23;
  memset(fake_flash, 0xFF, sizeof(fake_flash));
  memset(&v2.reload_flags, 0xAB, 4);   // whatever was in v2's padding must not leak into v3 fields
  memcpy(fake_flash, &v2, v2size - 4);
  uint32_t c = crc32(fake_flash, v2size - 4);
  memcpy(fake_flash + v2size - 4, &c, 4);
  settings_load();
  settings_t *s = &g_settings;
  CHECK(s->version == 3 && s->pad_count == 3 && s->smoothing == 5 && s->button_map[GB_A1] == XI_Y &&
        s->pad_type == PAD_DS4 && s->calib[1].valid && s->calib[1].h[0] == 0.25f && s->port_map[2] == 0x23,
        "v2 settings kept through migration");
  CHECK(s->reload_flags == 0 && s->reload_button == XI_B && s->reload_tap_ms == 60, "v3 fields take defaults");
  // and a v3 save round-trips
  s->reload_flags = 3; s->reload_button = XI_RB;
  settings_save();
  memset(s, 0, sizeof(*s));
  settings_load();
  CHECK(s->reload_flags == 3 && s->reload_button == XI_RB && s->pad_count == 3, "v3 save/load");
}

int main(void) {
  test_exit_tap();
  test_connect_offscreen();
  test_trigger();
  test_parked();
  test_off();
  test_migration();
  printf(fails ? "mapper/settings: %d FAILED\n" : "mapper/settings: all passed\n", fails);
  return fails != 0;
}
