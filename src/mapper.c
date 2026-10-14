#include <string.h>
#include <math.h>
#include "mapper.h"
#include "settings.h"
#include "status_led.h"

typedef struct {
  // smoothing / hold
  bool     have_pos;
  float    su, sv;
  int16_t  last_lx, last_ly;
  bool     has_last;
  uint32_t last_reports;
  // hotkeys
  bool     prev_c1, prev_c2, prev_trig;
  uint32_t c1_down, c2_down;
  bool     c1_rep, c2_rep;
  uint32_t c1_pulse, c2_pulse;
  bool     combo, combo_used;
  uint32_t combo_start;
} pad_state_t;

static pad_state_t s_ps[MAX_GUNS];
static pad_out_t   s_out[MAX_GUNS];
static int         s_aspect;
static bool        s_web_req;

static const float ASPECT_RATIO[ASPECT_COUNT] = { 0.0f, 4.0f / 3.0f, 3.0f / 4.0f, 5.0f / 4.0f, 0.0f };

void mapper_init(void) {
  memset(s_ps, 0, sizeof(s_ps));
  memset(s_out, 0, sizeof(s_out));
  s_aspect = g_settings.aspect_default;
}

int mapper_aspect(void) { return s_aspect; }
void mapper_set_aspect(int a) { if (a >= 0 && a < ASPECT_COUNT) s_aspect = a; }
const pad_out_t *mapper_pad(int i) { return &s_out[i]; }

bool mapper_take_webmode_request(void) {
  bool r = s_web_req;
  s_web_req = false;
  return r;
}

void mapper_game_rect(int aspect, float r[4]) {
  r[0] = 0; r[1] = 0; r[2] = 1; r[3] = 1;
  if (aspect == ASPECT_CUSTOM) { memcpy(r, g_settings.custom_rect, sizeof(float) * 4); return; }
  float a = (aspect > 0 && aspect < ASPECT_COUNT) ? ASPECT_RATIO[aspect] : 0.0f;
  float m = g_settings.monitor_aspect;
  if (a <= 0.0f || m <= 0.0f) return;
  if (a < m) {            // pillarbox
    float w = a / m;
    r[0] = (1.0f - w) * 0.5f; r[2] = r[0] + w;
  } else if (a > m) {     // letterbox
    float h = m / a;
    r[1] = (1.0f - h) * 0.5f; r[3] = r[1] + h;
  }
}

bool mapper_apply_h(const float h[8], float x, float y, float *u, float *v) {
  float w = h[6] * x + h[7] * y + 1.0f;
  if (fabsf(w) < 1e-9f) return false;
  *u = (h[0] * x + h[1] * y + h[2]) / w;
  *v = (h[3] * x + h[4] * y + h[5]) / w;
  return true;
}

static void cycle_aspect(void) {
  uint8_t mask = g_settings.aspect_cycle_mask;
  int a = s_aspect;
  for (int k = 0; k < ASPECT_COUNT; k++) {
    a = (a + 1) % ASPECT_COUNT;
    if (mask & (1u << a)) break;
  }
  s_aspect = a;
  led_flash(a + 1, LED_WHITE);   // blink count = mode index + 1
}

static inline bool btn(uint16_t b, int i) { return (b >> i) & 1; }

// Hotkeys: press C1+C2 together (within HOTKEY_WINDOW_MS). While held:
//   Trigger           -> cycle aspect mode
//   hold for 3 s      -> reboot into web configuration
// Lone C1/C2 presses are passed through (delayed by the window; quick taps become a pulse).
static uint16_t apply_hotkeys(pad_state_t *p, uint16_t b, uint32_t now, bool *suppress_trigger) {
  *suppress_trigger = false;
  bool c1 = btn(b, GB_C1), c2 = btn(b, GB_C2), trig = btn(b, GB_TRIGGER);

  if (!g_settings.hotkeys) {
    p->prev_c1 = c1; p->prev_c2 = c2; p->prev_trig = trig;
    return b;
  }

  if (c1 && !p->prev_c1) p->c1_down = now;
  if (c2 && !p->prev_c2) p->c2_down = now;

  if (p->combo) {
    if (!c1 && !c2) {
      p->combo = false;
      p->c1_rep = p->c2_rep = false;
    } else {
      if (trig && !p->prev_trig) { cycle_aspect(); p->combo_used = true; }
      if (c1 && c2 && !p->combo_used && (now - p->combo_start) >= HOTKEY_WEBMODE_MS) {
        p->combo_used = true;
        s_web_req = true;
      }
      *suppress_trigger = true;
    }
    b &= ~((1u << GB_C1) | (1u << GB_C2));
  } else {
    if (c1 && c2 && !p->c1_rep && !p->c2_rep) {
      p->combo = true; p->combo_used = false; p->combo_start = now;
      p->c1_pulse = p->c2_pulse = 0;
      b &= ~((1u << GB_C1) | (1u << GB_C2));
      *suppress_trigger = trig;   // a trigger already held stays suppressed until combo ends
    } else {
      if (c1 && (now - p->c1_down) >= HOTKEY_WINDOW_MS) p->c1_rep = true;
      if (c2 && (now - p->c2_down) >= HOTKEY_WINDOW_MS) p->c2_rep = true;
      if (!c1 && p->prev_c1) { if (!p->c1_rep) p->c1_pulse = now + 40; p->c1_rep = false; }
      if (!c2 && p->prev_c2) { if (!p->c2_rep) p->c2_pulse = now + 40; p->c2_rep = false; }
      bool o1 = p->c1_rep || (int32_t)(p->c1_pulse - now) > 0;
      bool o2 = p->c2_rep || (int32_t)(p->c2_pulse - now) > 0;
      b &= ~((1u << GB_C1) | (1u << GB_C2));
      if (o1) b |= 1u << GB_C1;
      if (o2) b |= 1u << GB_C2;
    }
  }

  p->prev_c1 = c1; p->prev_c2 = c2; p->prev_trig = trig;
  return b;
}

static int16_t to_axis(float n) {      // 0..1 -> -32768..32767
  float v = n * 65535.0f - 32768.0f;
  if (v < -32768.0f) v = -32768.0f;
  if (v > 32767.0f) v = 32767.0f;
  return (int16_t) lrintf(v);
}

static int16_t stick8_to_axis(uint8_t v, bool invert) {
  int32_t c = (int32_t) v - 128;
  if (invert) c = -c;
  int32_t a = c * 258;
  if (a > 32767) a = 32767;
  if (a < -32768) a = -32768;
  return (int16_t) a;
}

static void route_stick(int dest, uint8_t x, uint8_t y, xinput_report_t *r) {
  if (dest == DEST_RSTICK) {
    r->rx = stick8_to_axis(x, false);
    r->ry = stick8_to_axis(y, true);      // XInput: +Y is up
  } else if (dest == DEST_DPAD) {
    int dz = g_settings.dpad_deadzone;
    if (y < 128 - dz) r->buttons |= 0x0001;
    if (y > 128 + dz) r->buttons |= 0x0002;
    if (x < 128 - dz) r->buttons |= 0x0004;
    if (x > 128 + dz) r->buttons |= 0x0008;
  }
}

static void apply_target(int t, xinput_report_t *r) {
  static const uint16_t bits[XI_COUNT] = {
    [XI_A] = 0x1000, [XI_B] = 0x2000, [XI_X] = 0x4000, [XI_Y] = 0x8000,
    [XI_LB] = 0x0100, [XI_RB] = 0x0200, [XI_BACK] = 0x0020, [XI_START] = 0x0010,
    [XI_GUIDE] = 0x0400, [XI_LS] = 0x0040, [XI_RS] = 0x0080,
    [XI_DUP] = 0x0001, [XI_DDOWN] = 0x0002, [XI_DLEFT] = 0x0004, [XI_DRIGHT] = 0x0008,
  };
  if (t == XI_LT) r->lt = 255;
  else if (t == XI_RT) r->rt = 255;
  else if (t > XI_NONE && t < XI_COUNT) r->buttons |= bits[t];
}

void mapper_update(uint32_t now) {
  float rect[4];
  mapper_game_rect(s_aspect, rect);
  float rw = rect[2] - rect[0], rh = rect[3] - rect[1];
  float margin = g_settings.offscreen_margin / 100.0f;
  float alpha = 1.0f / (1.0f + 0.7f * g_settings.smoothing);

  for (int i = 0; i < MAX_GUNS; i++) {
    gun_raw_t g;
    guncon3_get(i, &g);
    pad_state_t *p = &s_ps[i];
    pad_out_t *o = &s_out[i];
    xinput_report_t r;
    memset(&r, 0, sizeof(r));
    r.msg_type = 0x00;
    r.length = 0x14;

    o->connected = g.connected;
    if (!g.connected) {
      memset(p, 0, sizeof(*p));
      o->calibrated = false; o->offscreen = false;
      o->rep = r;
      continue;
    }

    // --- aim ---
    // All-zero x/y/z = the gun sees no IR (pointing away / sensor not visible): no position at all
    bool no_aim = (g.x == 0 && g.y == 0 && g.z == 0);
    const gun_calib_t *c = &g_settings.calib[i];
    o->calibrated = c->valid;
    float u, v;
    if (!no_aim) {
      if (!(c->valid && mapper_apply_h(c->h, g.x, g.y, &u, &v))) {
        // Uncalibrated: pass raw straight through (useful only for checking the gun works)
        u = (g.x + 32768.0f) / 65535.0f;
        v = (g.y + 32768.0f) / 65535.0f;
      }
      if (g.reports != p->last_reports) {     // only filter on fresh reports
        p->last_reports = g.reports;
        if (!p->have_pos) { p->su = u; p->sv = v; p->have_pos = true; }
        else { p->su += alpha * (u - p->su); p->sv += alpha * (v - p->sv); }
      }
    } else {
      p->have_pos = false;                    // restart the filter when aim comes back
    }
    u = p->su; v = p->sv;
    o->u = u; o->v = v;

    float gx = rw > 0 ? (u - rect[0]) / rw : u;
    float gy = rh > 0 ? (v - rect[1]) / rh : v;
    bool off = no_aim || gx < -margin || gx > 1.0f + margin || gy < -margin || gy > 1.0f + margin;
    o->offscreen = off;

    int16_t lx, ly;
    if (off && g_settings.offscreen_mode == OFF_CORNER) {
      lx = -32768; ly = 32767;            // top-left corner
    } else if (off && (g_settings.offscreen_mode == OFF_HOLD || no_aim) && p->has_last) {
      lx = p->last_lx; ly = p->last_ly;   // hold (no-aim always holds, there's nothing to clamp)
    } else {
      float cx = gx < 0 ? 0 : (gx > 1 ? 1 : gx);
      float cy = gy < 0 ? 0 : (gy > 1 ? 1 : gy);
      lx = to_axis(cx);
      ly = to_axis(1.0f - cy);            // XInput +Y is up, screen +Y is down
      if (!off) { p->last_lx = lx; p->last_ly = ly; p->has_last = true; }
    }
    o->gx = (lx + 32768) / 65535.0f;
    o->gy = 1.0f - (ly + 32768) / 65535.0f;
    r.lx = lx; r.ly = ly;

    // --- buttons ---
    bool suppress_trig;
    uint16_t b = apply_hotkeys(p, g.buttons, now, &suppress_trig);
    if (suppress_trig) b &= ~(1u << GB_TRIGGER);
    for (int k = 0; k < GB_COUNT; k++) if (btn(b, k)) apply_target(g_settings.button_map[k], &r);

    // --- thumbsticks ---
    route_stick(g_settings.stick_a_dest, g.ax, g.ay, &r);
    route_stick(g_settings.stick_b_dest, g.bx, g.by, &r);

    o->rep = r;
  }
}
