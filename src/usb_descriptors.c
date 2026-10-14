// USB descriptors for both personalities:
//   XInput mode : composite device, one XInput interface per pad, MS OS 1.0 "XUSB10" compat IDs
//   Web mode    : RNDIS (Windows) / CDC-ECM (Linux, macOS) network adapter for the config page

#include <string.h>
#include "pico/unique_id.h"
#include "tusb.h"
#include "config.h"
#include "settings.h"
#include "usb_mode.h"
#include "ds4.h"

int g_usb_mode = BOOT_XINPUT;
int g_pads_active = 1;   // pads in the current enumeration (see main.c)
extern uint8_t tud_network_mac_address[6];

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL, STRID_INTERFACE, STRID_MAC };
#define MS_OS_VENDOR_CODE 0x90

//--------------------------------------------------------------------+
// Device descriptors
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device_xinput = {
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = 0x00,           // composite: lets Windows split interfaces (usbccgp)
  .bDeviceSubClass = 0x00,
  .bDeviceProtocol = 0x00,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor = GC3_XINPUT_VID,
  .idProduct = GC3_XINPUT_PID,
  .bcdDevice = 0x0110,            // patched with pad count (Windows caches per VID/PID/REV)
  .iManufacturer = STRID_MANUFACTURER,
  .iProduct = STRID_PRODUCT,
  .iSerialNumber = STRID_SERIAL,
  .bNumConfigurations = 1,
};

static tusb_desc_device_t const desc_device_web = {
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor = GC3_WEB_VID,
  .idProduct = GC3_WEB_PID,
  .bcdDevice = 0x0100,
  .iManufacturer = STRID_MANUFACTURER,
  .iProduct = STRID_PRODUCT,
  .iSerialNumber = STRID_SERIAL,
  .bNumConfigurations = 2,        // RNDIS, ECM
};

static tusb_desc_device_t s_dev;

static bool ds4_mode(void) { return g_usb_mode == BOOT_XINPUT && g_settings.pad_type == PAD_DS4; }

uint8_t const *tud_descriptor_device_cb(void) {
  if (g_usb_mode == BOOT_WEB) return (uint8_t const *) &desc_device_web;
  s_dev = desc_device_xinput;
  if (ds4_mode()) {
    s_dev.idVendor = DS4_VID;
    s_dev.idProduct = DS4_PID;
    s_dev.bcdDevice = 0x0100;
    return (uint8_t const *) &s_dev;
  }
  s_dev.bcdDevice = (uint16_t)(0x0110 + g_pads_active);
  return (uint8_t const *) &s_dev;
}

//--------------------------------------------------------------------+
// XInput configuration (built at runtime for 1..4 pads)
//--------------------------------------------------------------------+
#define XI_ITF_LEN (9 + 17 + 7 + 7)
static uint8_t s_cfg_xinput[9 + XI_ITF_LEN * MAX_GUNS];

static void build_xinput_config(void) {
  int n = g_pads_active;
  uint16_t total = (uint16_t)(9 + XI_ITF_LEN * n);
  uint8_t *p = s_cfg_xinput;
  uint8_t cfg[9] = { 9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(total), (uint8_t) n, 1, 0,
                     TU_BIT(7), 250 /* 500 mA */ };
  memcpy(p, cfg, 9); p += 9;
  for (int i = 0; i < n; i++) {
    uint8_t ep_in = 0x81 + i, ep_out = 0x01 + i;
    uint8_t itf[XI_ITF_LEN] = {
      // Interface: vendor 0xFF / 0x5D / 0x01 = Xbox 360 wired controller
      9, TUSB_DESC_INTERFACE, (uint8_t) i, 0, 2, 0xFF, 0x5D, 0x01, 0,
      // Vendor descriptor 0x21 found on genuine pads (endpoint addresses patched in)
      0x11, 0x21, 0x00, 0x01, 0x01, 0x25, ep_in, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, ep_out, 0x08, 0x00, 0x00,
      // Interrupt IN, 32 bytes, 1 ms
      7, TUSB_DESC_ENDPOINT, ep_in, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 1,
      // Interrupt OUT, 32 bytes, 8 ms
      7, TUSB_DESC_ENDPOINT, ep_out, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 8,
    };
    memcpy(p, itf, XI_ITF_LEN); p += XI_ITF_LEN;
  }
}

//--------------------------------------------------------------------+
// Web (network) configurations — from TinyUSB net_lwip_webserver example
//--------------------------------------------------------------------+
#define NET_BULK_EP_SIZE 64     // full-speed bulk
#define EPNUM_NET_NOTIF 0x81
#define EPNUM_NET_OUT   0x02
#define EPNUM_NET_IN    0x82
#define RNDIS_CFG_LEN (TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN)
#define ECM_CFG_LEN   (TUD_CONFIG_DESC_LEN + TUD_CDC_ECM_DESC_LEN)

static uint8_t const rndis_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, 2, 0, RNDIS_CFG_LEN, 0, 500),
  TUD_RNDIS_DESCRIPTOR(0, STRID_INTERFACE, EPNUM_NET_NOTIF, 8, EPNUM_NET_OUT, EPNUM_NET_IN, NET_BULK_EP_SIZE),
};

static uint8_t const ecm_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(2, 2, 0, ECM_CFG_LEN, 0, 500),
  TUD_CDC_ECM_DESCRIPTOR(0, STRID_INTERFACE, STRID_MAC, EPNUM_NET_NOTIF, 64, EPNUM_NET_OUT, EPNUM_NET_IN,
                         NET_BULK_EP_SIZE, CFG_TUD_NET_MTU),
};

// DualShock 4 configuration: one HID interface (IN + OUT) per pad
#define DS4_ITF_LEN TUD_HID_INOUT_DESC_LEN
static uint8_t s_cfg_ds4[9 + DS4_ITF_LEN * MAX_GUNS];

static void build_ds4_config(void) {
  int n = g_pads_active;
  uint16_t total = (uint16_t)(9 + DS4_ITF_LEN * n);
  uint8_t cfg[9] = { 9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(total), (uint8_t) n, 1, 0, TU_BIT(7), 250 };
  uint8_t *p = s_cfg_ds4;
  memcpy(p, cfg, 9); p += 9;
  for (int i = 0; i < n; i++) {
    uint8_t itf[DS4_ITF_LEN] = {
      TUD_HID_INOUT_DESCRIPTOR((uint8_t) i, 0, HID_ITF_PROTOCOL_NONE, ds4_report_descriptor_len,
                               (uint8_t)(0x01 + i), (uint8_t)(0x81 + i), 64, 1)
    };
    memcpy(p, itf, DS4_ITF_LEN); p += DS4_ITF_LEN;
  }
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  if (g_usb_mode == BOOT_WEB) {
    if (index == 0) return rndis_configuration;
    if (index == 1) return ecm_configuration;
    return NULL;
  }
  if (index != 0) return NULL;
  if (ds4_mode()) { build_ds4_config(); return s_cfg_ds4; }
  build_xinput_config();
  return s_cfg_xinput;
}

//--------------------------------------------------------------------+
// MS OS 1.0 descriptors (XInput mode): compat ID "XUSB10" on every pad interface
//--------------------------------------------------------------------+
static uint8_t s_compat[16 + 24 * MAX_GUNS];

static uint16_t build_compat_id(void) {
  int n = g_pads_active;
  uint32_t len = 16 + 24 * n;
  memset(s_compat, 0, sizeof(s_compat));
  s_compat[0] = len & 0xFF; s_compat[1] = (len >> 8) & 0xFF;
  s_compat[4] = 0x00; s_compat[5] = 0x01;        // bcdVersion 1.00
  s_compat[6] = 0x04; s_compat[7] = 0x00;        // wIndex 4 = extended compat ID
  s_compat[8] = (uint8_t) n;                     // function sections
  for (int i = 0; i < n; i++) {
    uint8_t *f = &s_compat[16 + 24 * i];
    f[0] = (uint8_t) i;                          // first interface
    f[1] = 0x01;
    memcpy(&f[2], "XUSB10", 6);
  }
  return (uint16_t) len;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req) {
  if (stage != CONTROL_STAGE_SETUP) return true;
  if (g_usb_mode != BOOT_XINPUT || ds4_mode()) return false;
  if (req->bRequest == MS_OS_VENDOR_CODE && req->wIndex == 0x0004) {
    uint16_t len = build_compat_id();
    return tud_control_xfer(rhport, req, s_compat, len < req->wLength ? len : req->wLength);
  }
  return false;
}

//--------------------------------------------------------------------+
// Strings
//--------------------------------------------------------------------+
static uint16_t s_str[40];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  unsigned n = 0;
  const char *str = NULL;
  char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

  switch (index) {
    case STRID_LANGID: s_str[1] = 0x0409; n = 1; break;
    case STRID_MANUFACTURER: str = ds4_mode() ? "Sony Interactive Entertainment" : "GC3X"; break;
    case STRID_PRODUCT: str = (g_usb_mode == BOOT_WEB) ? "GunCon 3 Adapter (Config)" : ds4_mode() ? "Wireless Controller" : "GunCon 3 XInput Adapter"; break;
    case STRID_SERIAL: pico_get_unique_board_id_string(serial, sizeof(serial)); str = serial; break;
    case STRID_INTERFACE: str = "GunCon 3 Adapter Config Network"; break;
    case STRID_MAC:
      for (unsigned i = 0; i < 6; i++) {
        s_str[1 + n++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 4) & 0xF];
        s_str[1 + n++] = "0123456789ABCDEF"[tud_network_mac_address[i] & 0xF];
      }
      break;
    case 0xEE:
      if (g_usb_mode != BOOT_XINPUT || ds4_mode()) return NULL;
      // MS OS 1.0 string: "MSFT100" + vendor code
      {
        static const char sig[] = "MSFT100";
        for (n = 0; n < 7; n++) s_str[1 + n] = sig[n];
        s_str[1 + n++] = MS_OS_VENDOR_CODE;
      }
      break;
    default: return NULL;
  }
  if (str) {
    size_t len = strlen(str);
    if (len > 39) len = 39;
    for (size_t i = 0; i < len; i++) s_str[1 + i] = (uint8_t) str[i];
    n = (unsigned) len;
  }
  s_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
  return s_str;
}
