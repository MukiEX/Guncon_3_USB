// Crash / hang diagnostics that survive a reboot, plus a TinyUSB host log ring buffer.
//
// * Hardware watchdog (3 s), kicked from core0 only while core1's heartbeat is moving.
//   A stuck core1 (or core0) therefore turns into a reboot back into the same mode,
//   instead of a dead adapter.
// * HardFault / panic on either core: record core, PC, LR, then reboot.
// * All of that, plus the last few KB of TinyUSB host log, lives in RAM that isn't
//   cleared at boot, so the config page can show what happened before the reboot.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/platform.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "diag.h"

#define PERSIST_MAGIC 0x44494147u   // 'DIAG'
#define LOG_SIZE      6144

typedef struct {
  uint32_t magic;
  uint32_t boots;
  uint32_t pending;                // reason recorded just before we rebooted on purpose
  uint32_t fault_core, fault_pc, fault_lr;
  char     panic_msg[80];
  uint32_t head;                   // total characters ever written
  char     log[LOG_SIZE];
} persist_t;

static persist_t __uninitialized_ram(s_p);

// Snapshot of the previous session, taken at boot
static uint32_t s_reason, s_prev_core, s_prev_pc, s_prev_lr;
static char     s_prev_panic[80];
static char     s_prev_log[LOG_SIZE + 1];

static spin_lock_t *s_lock;
static volatile uint32_t s_heartbeat;
static uint32_t s_last_hb, s_last_hb_ms;

static const char *reason_str(uint32_t r) {
  switch (r) {
    case RESET_POWER: return "power-on";
    case RESET_REQUESTED: return "requested (mode switch / page)";
    case RESET_WATCHDOG: return "watchdog: main core stopped";
    case RESET_CORE1_HANG: return "USB host core stopped responding";
    case RESET_FAULT: return "crash (HardFault)";
    case RESET_PANIC: return "panic";
    default: return "?";
  }
}

void diag_init(void) {
  s_lock = spin_lock_init(spin_lock_claim_unused(true));

  if (s_p.magic == PERSIST_MAGIC && s_p.head <= 0x7FFFFFFF) {
    // Previous session's data is valid: classify and snapshot it
    if (s_p.pending) s_reason = s_p.pending;
    else s_reason = watchdog_caused_reboot() ? RESET_WATCHDOG : RESET_POWER;
    s_prev_core = s_p.fault_core; s_prev_pc = s_p.fault_pc; s_prev_lr = s_p.fault_lr;
    memcpy(s_prev_panic, s_p.panic_msg, sizeof(s_prev_panic));
    s_prev_panic[sizeof(s_prev_panic) - 1] = 0;
    // Unroll the ring into a linear string
    uint32_t n = s_p.head < LOG_SIZE ? s_p.head : LOG_SIZE;
    uint32_t start = s_p.head - n;
    for (uint32_t i = 0; i < n; i++) s_prev_log[i] = s_p.log[(start + i) % LOG_SIZE];
    s_prev_log[n] = 0;
    s_p.boots++;
  } else {
    s_reason = RESET_POWER;
    s_prev_log[0] = 0;
    s_prev_panic[0] = 0;
    memset(&s_p, 0, sizeof(s_p));
    s_p.magic = PERSIST_MAGIC;
  }
  s_p.pending = 0;
  s_p.fault_core = s_p.fault_pc = s_p.fault_lr = 0;
  s_p.panic_msg[0] = 0;
  s_p.head = 0;

  diag_log("boot #%lu, last reset: %s\n", (unsigned long) s_p.boots, reason_str(s_reason));
  watchdog_enable(3000, true);
  s_last_hb_ms = to_ms_since_boot(get_absolute_time());
}

void diag_core1_heartbeat(void) { s_heartbeat++; }

static void __attribute__((noreturn)) __not_in_flash_func(reboot_with)(uint32_t reason) {
  s_p.pending = reason;
  watchdog_reboot(0, 0, 0);
  while (1) tight_loop_contents();
}

void diag_note_reboot_requested(void) { s_p.pending = RESET_REQUESTED; }

void diag_supervise(uint32_t now) {
  uint32_t hb = s_heartbeat;
  if (hb != s_last_hb) { s_last_hb = hb; s_last_hb_ms = now; }
  else if (now - s_last_hb_ms > 2000) {
    diag_log("core1 heartbeat stopped for 2 s, rebooting\n");
    reboot_with(RESET_CORE1_HANG);
  }
  watchdog_update();
}

//--------------------------------------------------------------------+
// Log ring
//--------------------------------------------------------------------+
static void ring_put(const char *s, int n) {
  uint32_t l = spin_lock_blocking(s_lock);
  for (int i = 0; i < n; i++) s_p.log[(s_p.head++) % LOG_SIZE] = s[i];
  spin_unlock(s_lock, l);
}

static void vlog(const char *fmt, va_list ap) {
  char buf[160];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n <= 0) return;
  if (n >= (int) sizeof(buf)) n = sizeof(buf) - 1;
  ring_put(buf, n);
}

void diag_log(const char *fmt, ...) {
  char tag[16];
  int t = snprintf(tag, sizeof(tag), "%lu ", (unsigned long) to_ms_since_boot(get_absolute_time()));
  ring_put(tag, t);
  va_list ap; va_start(ap, fmt); vlog(fmt, ap); va_end(ap);
}

// TinyUSB host log. Per-transfer chatter (every gun report) is dropped before formatting
// so the ring keeps enumeration / hub / error history instead.
int diag_printf(const char *fmt, ...) {
  static const char *const skip[] = { "on EP %02X", "xfer callback", "Claimed EP", "Released EP", "Queue EP" };
  for (unsigned i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) if (strstr(fmt, skip[i])) return 0;
  va_list ap; va_start(ap, fmt); vlog(fmt, ap); va_end(ap);
  return 0;
}

//--------------------------------------------------------------------+
// Fault / panic capture
//--------------------------------------------------------------------+
void __used __not_in_flash_func(diag_hardfault_c)(uint32_t *frame) {
  s_p.fault_core = get_core_num();
  s_p.fault_pc = frame[6];
  s_p.fault_lr = frame[5];
  reboot_with(RESET_FAULT);
}

// Overrides the SDK's weak handler (same vector on both cores)
void __attribute__((naked, used)) __not_in_flash_func(isr_hardfault)(void) {
  __asm volatile(
    "movs r0, #4        \n"
    "mov  r1, lr        \n"
    "tst  r0, r1        \n"
    "beq  1f            \n"
    "mrs  r0, psp       \n"
    "b    2f            \n"
    "1: mrs r0, msp     \n"
    "2: ldr r2, =diag_hardfault_c \n"
    "bx   r2            \n"
    ".ltorg             \n"
  );
}

void __attribute__((noreturn)) diag_panic(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vsnprintf(s_p.panic_msg, sizeof(s_p.panic_msg), fmt ? fmt : "panic", ap);
  va_end(ap);
  s_p.fault_core = get_core_num();
  reboot_with(RESET_PANIC);
}

//--------------------------------------------------------------------+
// JSON for the config page
//--------------------------------------------------------------------+
static int json_str(char *out, size_t len, const char *s) {
  size_t o = 0;
  if (o < len) out[o++] = '"';
  for (; *s && o + 7 < len; s++) {
    unsigned char ch = (unsigned char) *s;
    if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = ch; }
    else if (ch == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
    else if (ch == '\r') {}
    else if (ch < 0x20 || ch > 0x7E) o += snprintf(out + o, len - o, "\\u%04x", ch);
    else out[o++] = ch;
  }
  if (o < len) out[o++] = '"';
  return (int) o;
}

int diag_json(char *buf, size_t len) {
  static char cur[LOG_SIZE + 1];
  uint32_t l = spin_lock_blocking(s_lock);
  uint32_t n = s_p.head < LOG_SIZE ? s_p.head : LOG_SIZE, start = s_p.head - n;
  for (uint32_t i = 0; i < n; i++) cur[i] = s_p.log[(start + i) % LOG_SIZE];
  cur[n] = 0;
  spin_unlock(s_lock, l);

  int o = snprintf(buf, len, "{\"boots\":%lu,\"lastReset\":\"%s\",\"faultCore\":%lu,\"faultPC\":\"0x%08lx\",\"faultLR\":\"0x%08lx\",\"panic\":",
                   (unsigned long) s_p.boots, reason_str(s_reason), (unsigned long) s_prev_core,
                   (unsigned long) s_prev_pc, (unsigned long) s_prev_lr);
  o += json_str(buf + o, len - o, s_prev_panic);
  o += snprintf(buf + o, len - o, ",\"prevLog\":");
  o += json_str(buf + o, len - o, s_prev_log);
  o += snprintf(buf + o, len - o, ",\"log\":");
  o += json_str(buf + o, len - o, cur);
  o += snprintf(buf + o, len - o, "}");
  return o < (int) len ? o : (int) len - 1;
}
