// Minimal XInput (Xbox 360 wired protocol) device class driver for TinyUSB.
// Supports several XInput interfaces in one composite device: one per pad.

#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "xinput_dev.h"

typedef struct {
  uint8_t itf;
  uint8_t ep_in, ep_out;
  bool    open;
  uint8_t last[20];
  uint32_t last_ms;
} xi_pad_t;

static xi_pad_t s_pad[XINPUT_MAX_PADS];
static int s_count;
CFG_TUD_MEM_SECTION static struct {
  TUD_EPBUF_DEF(in, 32);
  TUD_EPBUF_DEF(out, 32);
} s_buf[XINPUT_MAX_PADS];

static void xi_init(void) { memset(s_pad, 0, sizeof(s_pad)); s_count = 0; }
static bool xi_deinit(void) { return true; }
static void xi_reset(uint8_t rhport) { (void) rhport; xi_init(); }

static uint16_t xi_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len) {
  if (itf->bInterfaceClass != 0xFF || itf->bInterfaceSubClass != 0x5D || itf->bInterfaceProtocol != 0x01) return 0;
  if (s_count >= XINPUT_MAX_PADS) return 0;

  xi_pad_t *p = &s_pad[s_count];
  memset(p, 0, sizeof(*p));
  p->itf = itf->bInterfaceNumber;

  uint8_t const *d = (uint8_t const *) itf;
  uint16_t len = tu_desc_len(d);
  d = tu_desc_next(d);
  int eps = 0;
  while (len < max_len && eps < itf->bNumEndpoints) {
    if (tu_desc_type(d) == TUSB_DESC_INTERFACE) break;
    if (tu_desc_type(d) == TUSB_DESC_ENDPOINT) {
      tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *) d;
      if (!usbd_edpt_open(rhport, ep)) return 0;
      if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) p->ep_in = ep->bEndpointAddress;
      else p->ep_out = ep->bEndpointAddress;
      eps++;
    }
    len += tu_desc_len(d);
    d = tu_desc_next(d);
  }
  if (!p->ep_in) return 0;

  if (p->ep_out) usbd_edpt_xfer(rhport, p->ep_out, s_buf[s_count].out, 32);
  p->open = true;
  s_count++;
  return len;
}

static bool xi_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req) {
  (void) rhport; (void) stage; (void) req;
  return false;   // stall: wired-pad vendor queries are optional for xusb22 / xpad
}

static bool xi_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t len) {
  (void) result; (void) len;
  for (int i = 0; i < s_count; i++) {
    if (ep_addr == s_pad[i].ep_out) {
      // Rumble / LED-ring commands from the host: nothing to drive on a GunCon, just re-arm
      usbd_edpt_xfer(rhport, ep_addr, s_buf[i].out, 32);
      return true;
    }
    if (ep_addr == s_pad[i].ep_in) return true;
  }
  return false;
}

static usbd_class_driver_t const s_driver = {
  .name = "XINPUT",
  .init = xi_init,
  .deinit = xi_deinit,
  .reset = xi_reset,
  .open = xi_open,
  .control_xfer_cb = xi_control_xfer_cb,
  .xfer_cb = xi_xfer_cb,
  .sof = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count) {
  *count = 1;
  return &s_driver;
}

bool xinput_send(int pad, const void *report20, uint32_t now_ms) {
  if (pad >= s_count || !s_pad[pad].open || !tud_ready()) return false;
  xi_pad_t *p = &s_pad[pad];
  // Send on change, plus a keep-alive every 8 ms
  if (!memcmp(p->last, report20, 20) && (now_ms - p->last_ms) < 8) return false;
  if (tud_suspended()) return false;
  if (usbd_edpt_busy(0, p->ep_in)) return false;
  if (!usbd_edpt_claim(0, p->ep_in)) return false;
  memcpy(s_buf[pad].in, report20, 20);
  if (!usbd_edpt_xfer(0, p->ep_in, s_buf[pad].in, 20)) {
    usbd_edpt_release(0, p->ep_in);
    return false;
  }
  memcpy(p->last, report20, 20);
  p->last_ms = now_ms;
  return true;
}
