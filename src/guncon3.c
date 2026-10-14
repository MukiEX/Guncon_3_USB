// GunCon 3 USB host driver.
//
// The GunCon 3 (Namco, VID 0B9A PID 0800) is a vendor-class device with one
// interrupt OUT and one interrupt IN endpoint. The host sends an 8-byte key once,
// after which every 15-byte IN report is obfuscated with that key. Decoder
// ported from beardypig/guncon3 (Linux) and gameotaku79/Guncon3Windows.
//
// Runs on core1 together with the PIO-USB host stack. Up to MAX_GUNS guns, through hubs.
// Player numbers: either in plug-in order, or fixed per port path via the saved port map.

#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/sync.h"
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "host/hcd.h"
#include "pio_usb_ll.h"
#include "pio_usb.h"

#include "guncon3.h"
#include "diag.h"
#include "settings.h"

#define GC3_VID 0x0B9A
#define GC3_PID 0x0800

//--------------------------------------------------------------------+
// Shared state (written on core1, read on core0)
//--------------------------------------------------------------------+
static gun_raw_t g_state[MAX_GUNS];
static spin_lock_t *g_lock;

void guncon3_get(int slot, gun_raw_t *out) {
  uint32_t s = spin_lock_blocking(g_lock);
  *out = g_state[slot];
  spin_unlock(g_lock, s);
}

//--------------------------------------------------------------------+
// Per-device driver state (core1 only)
//--------------------------------------------------------------------+
#define HIST 8
typedef struct {
  uint8_t daddr;          // 0 = free
  uint8_t slot;           // player index this gun currently drives
  uint16_t path;
  uint8_t itf_num;
  uint8_t ep_in, ep_out;
  uint16_t mps_in;
  bool    key_sent;
  uint8_t bad_run;
  bool    prev_trigger;
  int16_t hx[HIST], hy[HIST];
  uint8_t hpos, hcount;
} gun_dev_t;

static gun_dev_t g_dev[MAX_GUNS];
CFG_TUH_MEM_SECTION static struct {
  TUH_EPBUF_DEF(in, 64);
  TUH_EPBUF_DEF(out, 8);
} g_buf[MAX_GUNS];

// g_dev[] index = device record (owns the transfer buffers, never moves).
// g_dev[i].slot = player number it feeds (g_state[] index), can be re-seated.
static int dev_of(uint8_t daddr) {
  for (int i = 0; i < MAX_GUNS; i++) if (g_dev[i].daddr == daddr) return i;
  return -1;
}

static bool slot_taken(int slot, int except_dev) {
  for (int i = 0; i < MAX_GUNS; i++) if (i != except_dev && g_dev[i].daddr && g_dev[i].slot == slot) return true;
  return false;
}

// Port path of a gun: walk up from its built-in hub (or the gun itself) to the adapter
static uint16_t path_of(uint8_t daddr) {
  tuh_bus_info_t bi;
  uint8_t cur = daddr;
  if (tuh_bus_info_get(cur, &bi) && bi.hub_addr) {
    uint16_t v, p;
    if (tuh_vid_pid_get(bi.hub_addr, &v, &p) && v == 0x0C12 && p == 0x8801) cur = bi.hub_addr;
  }
  uint8_t ports[4]; int n = 0;
  while (n < 4 && tuh_bus_info_get(cur, &bi) && bi.hub_addr) {
    ports[n++] = bi.hub_port & 0x0F;
    cur = bi.hub_addr;
  }
  uint16_t code = 0;
  for (int i = n - 1; i >= 0; i--) code = (uint16_t)((code << 4) | ports[i]);
  return code;
}

static int pick_slot(uint16_t path, int except_dev) {
  if (g_settings.player_mode == PLAYERS_BY_PORT) {
    for (int i = 0; i < MAX_GUNS; i++)
      if (g_settings.port_map[i] == path && !slot_taken(i, except_dev)) return i;
    // Unmapped port: prefer a player number no port is reserved for
    for (int i = 0; i < MAX_GUNS; i++)
      if (g_settings.port_map[i] == PORT_PATH_NONE && !slot_taken(i, except_dev)) return i;
  }
  for (int i = 0; i < MAX_GUNS; i++) if (!slot_taken(i, except_dev)) return i;
  return -1;
}

static bool queue_in(int s) {   // s = device record
  gun_dev_t *g = &g_dev[s];
  if (!usbh_edpt_claim(g->daddr, g->ep_in)) return false;
  if (!usbh_edpt_xfer(g->daddr, g->ep_in, g_buf[s].in, g->mps_in)) {
    usbh_edpt_release(g->daddr, g->ep_in);
    return false;
  }
  return true;
}

static bool send_key(int s) {
  gun_dev_t *g = &g_dev[s];
  memcpy(g_buf[s].out, guncon3_key, 8);
  if (!usbh_edpt_claim(g->daddr, g->ep_out)) return false;
  if (!usbh_edpt_xfer(g->daddr, g->ep_out, g_buf[s].out, 8)) {
    usbh_edpt_release(g->daddr, g->ep_out);
    return false;
  }
  return true;
}

static void handle_report(int s, const uint8_t *data) {   // s = device record
  gun_dev_t *g = &g_dev[s];
  uint8_t d[13];
  if (!guncon3_decode(data, guncon3_key, d)) {
    uint32_t l = spin_lock_blocking(g_lock);
    g_state[g->slot].bad++;
    spin_unlock(g_lock, l);
    // Gun may have power-cycled behind a hub or lost its key; re-send it
    if (++g->bad_run >= 16) { g->bad_run = 0; send_key(s); }
    return;
  }
  g->bad_run = 0;

  uint16_t b = 0;
  if (d[1] & 0x20) b |= 1u << GB_TRIGGER;
  if (d[0] & 0x04) b |= 1u << GB_A1;
  if (d[0] & 0x02) b |= 1u << GB_A2;
  if (d[1] & 0x04) b |= 1u << GB_B1;
  if (d[1] & 0x02) b |= 1u << GB_B2;
  if (d[1] & 0x80) b |= 1u << GB_C1;
  if (d[0] & 0x08) b |= 1u << GB_C2;
  if (d[2] & 0x80) b |= 1u << GB_ACLICK;
  if (d[2] & 0x40) b |= 1u << GB_BCLICK;

  int16_t x = (int16_t)((d[4] << 8) | d[3]);
  int16_t y = (int16_t)((d[6] << 8) | d[5]);
  int16_t z = (int16_t)((d[8] << 8) | d[7]);

  bool trig = b & (1u << GB_TRIGGER);
  bool shot = trig && !g->prev_trigger;
  g->prev_trigger = trig;

  int32_t sx = 0, sy = 0;
  if (shot) {
    // Average history excluding the newest entry (the trigger pull itself can jerk the aim)
    int n = g->hcount > 1 ? g->hcount - 1 : 0;
    if (n > 5) n = 5;
    if (n == 0) { sx = x; sy = y; }
    else {
      for (int k = 1; k <= n; k++) {
        int idx = (g->hpos + HIST - 1 - k) % HIST;
        sx += g->hx[idx]; sy += g->hy[idx];
      }
      sx /= n; sy /= n;
    }
  }
  g->hx[g->hpos] = x; g->hy[g->hpos] = y;
  g->hpos = (g->hpos + 1) % HIST;
  if (g->hcount < HIST) g->hcount++;

  uint32_t l = spin_lock_blocking(g_lock);
  gun_raw_t *st = &g_state[g->slot];
  if (x != st->x || y != st->y) st->moves++;
  st->buttons = b;
  st->x = x; st->y = y; st->z = z;
  st->ax = d[11]; st->ay = d[12];
  st->bx = d[9];  st->by = d[10];
  st->ind = ((d[1] & 0x10) ? 1 : 0) | ((d[1] & 0x08) ? 2 : 0);
  st->reports++;
  st->last_ms = to_ms_since_boot(get_absolute_time());
  if (shot) { st->shot_seq++; st->shot_x = (int16_t)sx; st->shot_y = (int16_t)sy; }
  spin_unlock(g_lock, l);
}

//--------------------------------------------------------------------+
// TinyUSB application class driver
//--------------------------------------------------------------------+
static bool gc3_init(void) { memset(g_dev, 0, sizeof(g_dev)); return true; }
static bool gc3_deinit(void) { return true; }

static uint16_t gc3_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const *itf, uint16_t max_len) {
  (void) rhport;
  uint16_t vid, pid;
  if (!tuh_vid_pid_get(daddr, &vid, &pid) || vid != GC3_VID || pid != GC3_PID) return 0;
  if (itf->bNumEndpoints < 2) return 0;

  uint16_t path = path_of(daddr);
  int s = -1;   // free device record
  for (int i = 0; i < MAX_GUNS; i++) if (g_dev[i].daddr == 0) { s = i; break; }
  if (s < 0) return 0;
  int slot = pick_slot(path, -1);
  if (slot < 0) return 0;

  gun_dev_t *g = &g_dev[s];
  memset(g, 0, sizeof(*g));

  uint8_t const *p = (uint8_t const *) itf;
  uint8_t const *end = p + max_len;
  p = tu_desc_next(p);
  int found = 0;
  while (p < end) {
    if (tu_desc_type(p) == TUSB_DESC_INTERFACE) break;
    if (tu_desc_type(p) == TUSB_DESC_ENDPOINT) {
      tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *) p;
      if (!tuh_edpt_open(daddr, ep)) return 0;
      if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
        g->ep_in = ep->bEndpointAddress;
        g->mps_in = tu_edpt_packet_size(ep);
        if (g->mps_in > 64) g->mps_in = 64;
      } else {
        g->ep_out = ep->bEndpointAddress;
      }
      found++;
    }
    p = tu_desc_next(p);
    if (found >= itf->bNumEndpoints) break;
  }
  if (!g->ep_in || !g->ep_out) return 0;

  g->daddr = daddr;
  g->itf_num = itf->bInterfaceNumber;
  g->slot = (uint8_t) slot;
  g->path = path;

  uint32_t l = spin_lock_blocking(g_lock);
  memset(&g_state[slot], 0, sizeof(g_state[slot]));
  g_state[slot].path = path;
  spin_unlock(g_lock, l);
  diag_log("GunCon on port path %x -> player %d\n", path, slot + 1);
  return (uint16_t)(p - (uint8_t const *) itf);
}

static bool gc3_set_config(uint8_t daddr, uint8_t itf_num) {
  int s = dev_of(daddr);
  if (s >= 0) {
    send_key(s);
    uint32_t l = spin_lock_blocking(g_lock);
    g_state[g_dev[s].slot].connected = true;
    g_state[g_dev[s].slot].last_ms = to_ms_since_boot(get_absolute_time());
    spin_unlock(g_lock, l);
  }
  usbh_driver_set_config_complete(daddr, itf_num);
  return true;
}

static bool gc3_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t len) {
  int s = dev_of(daddr);
  if (s < 0) return false;
  gun_dev_t *g = &g_dev[s];

  if (ep_addr == g->ep_out) {
    if (!g->key_sent) { g->key_sent = true; queue_in(s); }
    return true;
  }
  if (ep_addr == g->ep_in) {
    if (result == XFER_RESULT_SUCCESS && len >= 15) handle_report(s, g_buf[s].in);
    queue_in(s);
    return true;
  }
  return false;
}

static void gc3_close(uint8_t daddr) {
  int s = dev_of(daddr);
  if (s < 0) return;
  uint8_t slot = g_dev[s].slot;
  memset(&g_dev[s], 0, sizeof(g_dev[s]));
  uint32_t l = spin_lock_blocking(g_lock);
  memset(&g_state[slot], 0, sizeof(g_state[slot]));
  spin_unlock(g_lock, l);
}

// Re-seat connected guns after the player mapping changed (runs on core1)
static volatile bool s_reassign;
void guncon3_request_reassign(void) { s_reassign = true; }

static void do_reassign(void) {
  if (!s_reassign) return;
  s_reassign = false;
  if (g_settings.player_mode != PLAYERS_BY_PORT) return;   // plug order: keep current seats
  int8_t want[MAX_GUNS];
  bool used[MAX_GUNS] = {0};
  // 1) guns whose port is mapped get that player number
  for (int d = 0; d < MAX_GUNS; d++) {
    want[d] = -1;
    if (!g_dev[d].daddr) continue;
    for (int i = 0; i < MAX_GUNS; i++)
      if (g_settings.port_map[i] == g_dev[d].path && !used[i]) { want[d] = (int8_t) i; used[i] = true; break; }
  }
  // 2) the rest keep their seat if still free, else take the first free one
  for (int d = 0; d < MAX_GUNS; d++) {
    if (!g_dev[d].daddr || want[d] >= 0) continue;
    int cur = g_dev[d].slot;
    if (!used[cur]) { want[d] = (int8_t) cur; used[cur] = true; continue; }
    for (int i = 0; i < MAX_GUNS; i++) if (!used[i]) { want[d] = (int8_t) i; used[i] = true; break; }
  }
  gun_raw_t old[MAX_GUNS];
  uint32_t l = spin_lock_blocking(g_lock);
  memcpy(old, g_state, sizeof(old));
  memset(g_state, 0, sizeof(g_state));
  for (int d = 0; d < MAX_GUNS; d++) {
    if (!g_dev[d].daddr || want[d] < 0) continue;
    g_state[want[d]] = old[g_dev[d].slot];
    g_dev[d].slot = (uint8_t) want[d];
  }
  spin_unlock(g_lock, l);
  diag_log("players re-seated from the port map\n");
}

static usbh_class_driver_t const gc3_driver = {
  .name = "GUNCON3",
  .init = gc3_init,
  .deinit = gc3_deinit,
  .open = gc3_open,
  .set_config = gc3_set_config,
  .xfer_cb = gc3_xfer_cb,
  .close = gc3_close,
};

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1;
  return &gc3_driver;
}

//--------------------------------------------------------------------+
// USB device list (diagnostics for the config page): every device the host
// stack finishes enumerating, hubs included
//--------------------------------------------------------------------+
static usb_dev_info_t g_usb[USB_DIAG_MAX];
static uint32_t g_usb_events;

void tuh_mount_cb(uint8_t daddr) {
  usb_dev_info_t d = {0};
  d.addr = daddr;
  tuh_vid_pid_get(daddr, &d.vid, &d.pid);
  tuh_bus_info_t bi;
  if (tuh_bus_info_get(daddr, &bi)) { d.hub_addr = bi.hub_addr; d.hub_port = bi.hub_port; d.speed = bi.speed; }
  uint32_t l = spin_lock_blocking(g_lock);
  for (int i = 0; i < USB_DIAG_MAX; i++) if (!g_usb[i].addr) { g_usb[i] = d; break; }
  g_usb_events++;
  spin_unlock(g_lock, l);
}

void tuh_umount_cb(uint8_t daddr) {
  uint32_t l = spin_lock_blocking(g_lock);
  for (int i = 0; i < USB_DIAG_MAX; i++) if (g_usb[i].addr == daddr) memset(&g_usb[i], 0, sizeof(g_usb[i]));
  g_usb_events++;
  spin_unlock(g_lock, l);
}

int guncon3_usb_list(usb_dev_info_t *out, int max, uint32_t *events) {
  int n = 0;
  uint32_t l = spin_lock_blocking(g_lock);
  for (int i = 0; i < USB_DIAG_MAX && n < max; i++) if (g_usb[i].addr) out[n++] = g_usb[i];
  if (events) *events = g_usb_events;
  spin_unlock(g_lock, l);
  // Hubs don't get tuh_mount_cb; their addresses start after CFG_TUH_DEVICE_MAX
  for (uint8_t a = CFG_TUH_DEVICE_MAX + 1; a <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB && n < max; a++) {
    usb_dev_info_t d = {0};
    if (!tuh_vid_pid_get(a, &d.vid, &d.pid)) continue;
    tuh_bus_info_t bi;
    if (tuh_bus_info_get(a, &bi)) { d.hub_addr = bi.hub_addr; d.hub_port = bi.hub_port; d.speed = bi.speed; }
    d.addr = a;
    d.is_hub = 1;
    out[n++] = d;
  }
  return n;
}

// TinyUSB never retries a failed enumeration on the root port by itself: it waits for
// a physical disconnect. Some hubs miss the first attempt right after power-up, so if
// something is attached but nothing has enumerated for a while, fake an unplug/replug.
// PIO-USB detects a root-port unplug from the D+/D- line state, which has proved unreliable
// here. So also probe whatever sits on the root port with a GET_STATUS every 500 ms; two
// failures in a row = it's gone, and we raise the same disconnect PIO-USB would have.
static volatile int8_t s_probe_result;   // 0 = idle, 1 = in flight
static uint8_t s_probe_fails;
CFG_TUH_MEM_SECTION static uint8_t s_probe_buf[4] __attribute__((aligned(4)));

static void probe_cb(tuh_xfer_t *xfer) {
  if (xfer->result == XFER_RESULT_SUCCESS) s_probe_fails = 0;
  else s_probe_fails++;
  s_probe_result = 0;
}

static uint8_t root_device_addr(void) {
  for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; a++) {
    tuh_bus_info_t bi;
    if (tuh_mounted(a) && tuh_bus_info_get(a, &bi) && bi.hub_addr == 0) return a;
  }
  return 0;
}

static void force_root_disconnect(void) {
  root_port_t *root = PIO_USB_ROOT_PORT(0);
  uint32_t irq = save_and_disable_interrupts();
  if (root->connected) {
    root->connected = false;
    root->suspended = true;
    root->ints |= PIO_USB_INTS_DISCONNECT_BITS;
    for (int i = 0; i < PIO_USB_EP_POOL_CNT; i++) {
      endpoint_t *ep = PIO_USB_ENDPOINT(i);
      if (ep->root_idx == 0 && ep->size && ep->has_transfer)
        pio_usb_ll_transfer_complete(ep, PIO_USB_INTS_ENDPOINT_ERROR_BITS);
    }
  }
  restore_interrupts(irq);
}

static void root_probe(uint32_t now) {
  static uint32_t last;
  if (now - last < 500 || s_probe_result) return;
  last = now;
  uint8_t a = root_device_addr();
  if (!a) { s_probe_fails = 0; return; }
  if (s_probe_fails >= 2) {
    diag_log("root port: device %u stopped answering, treating as unplugged\n", a);
    s_probe_fails = 0;
    force_root_disconnect();
    return;
  }
  static tusb_control_request_t req = {
    .bmRequestType_bit = { .recipient = TUSB_REQ_RCPT_DEVICE, .type = TUSB_REQ_TYPE_STANDARD, .direction = TUSB_DIR_IN },
    .bRequest = TUSB_REQ_GET_STATUS, .wValue = 0, .wIndex = 0, .wLength = 2
  };
  tuh_xfer_t x = { .daddr = a, .ep_addr = 0, .setup = &req, .buffer = s_probe_buf, .complete_cb = probe_cb, .user_data = 0 };
  s_probe_result = 1;
  if (!tuh_control_xfer(&x)) s_probe_result = 0;   // control pipe busy: try next round
}

static bool anything_mounted(void) {
  for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; a++) if (tuh_mounted(a)) return true;
  return false;
}

static void root_port_watchdog(uint32_t *idle_since, uint32_t *gap) {
  uint32_t now = to_ms_since_boot(get_absolute_time());
  root_probe(now);
  if (!hcd_port_connect_status(1) || anything_mounted()) {
    *idle_since = 0;
    *gap = 2000;
    return;
  }
  if (!*idle_since) { *idle_since = now; return; }
  if (now - *idle_since < *gap) return;
  diag_log("root port: device attached but not enumerated for %lu ms, retrying\n", (unsigned long) *gap);
  hcd_event_device_remove(1, false);
  hcd_event_device_attach(1, false);
  *idle_since = now;
  if (*gap < 8000) *gap *= 2;   // 2 s, 4 s, 8 s, 8 s ...
}

//--------------------------------------------------------------------+
// Core1 entry
//--------------------------------------------------------------------+
void guncon3_host_core1_entry(void) {
  // Let core0 pause us safely while it writes settings to flash
  flash_safe_execute_core_init();

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = GC3_PIO_USB_DP_PIN;
#if NUM_PIOS > 2
  // RP2350: keep PIO2 free for the status LED
  pio_cfg.pio_tx_num = 0;
  pio_cfg.pio_rx_num = 1;
#endif
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tusb_rhport_init_t host_init = { .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO };
  tusb_init(1, &host_init);

  uint32_t idle_since = 0, next_retry_gap = 2000;
  while (true) {
    diag_core1_heartbeat();
    tuh_task_ext(10, false);   // return at least every 10 ms so the heartbeat keeps moving
    root_port_watchdog(&idle_since, &next_retry_gap);
    do_reassign();
  }
}

void guncon3_init_shared(void) {
  g_lock = spin_lock_init(spin_lock_claim_unused(true));
  memset(g_state, 0, sizeof(g_state));
}
