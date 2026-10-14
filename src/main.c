// GunCon 3 -> XInput adapter
//
// core0: USB device (XInput pads, or RNDIS/ECM + web config), mapping, LED, BOOTSEL
// core1: PIO-USB host stack + GunCon 3 driver (hub supported)

#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "tusb.h"

#include "config.h"
#include "settings.h"
#include "guncon3.h"
#include "mapper.h"
#include "xinput_dev.h"
#include "usb_mode.h"
#include "status_led.h"
#include "net.h"
#include "http.h"
#include "diag.h"
#include "ds4.h"

//--------------------------------------------------------------------+
// BOOTSEL button (read through the QSPI CS line; flash is unusable meanwhile,
// so core1 is parked through flash_safe_execute while we look)
//--------------------------------------------------------------------+
#if PICO_RP2040
  #define CS_PIN_INDEX 1
  #define CS_BIT (1u << 1)
#else
  #define CS_PIN_INDEX 2
  #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif

static void __no_inline_not_in_flash_func(read_bootsel)(void *param) {
  bool *out = param;
  hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                  GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                  IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
  for (volatile int i = 0; i < 1000; ++i) {}
  *out = !(sio_hw->gpio_hi_in & CS_BIT);
  hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                  GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                  IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
}

static bool bootsel_pressed(void) {
  bool pressed = false;
  if (flash_safe_execute(read_bootsel, &pressed, 20) != PICO_OK) {
    diag_log("BOOT check: USB host core didn't pause\n");
    return false;
  }
  return pressed;
}

//--------------------------------------------------------------------+
// Plug-order mode: re-enumerate with exactly as many pads as connected guns.
// Changes are debounced so plugging in several guns at once costs one reconnect.
//--------------------------------------------------------------------+
static void pad_manager(uint32_t now) {
  static uint32_t changed_at;
  static int pending = -1;
  int want = 0;
  for (int i = 0; i < MAX_GUNS; i++) if (mapper_pad(i)->connected) want = i + 1;   // seats are compact
  if (want > g_settings.pad_count) want = g_settings.pad_count;
  if (want == g_pads_active) { pending = -1; return; }
  if (want != pending) { pending = want; changed_at = now; return; }
  if (now - changed_at < (g_pads_active ? 400u : 1200u)) return;   // first connect: let all guns enumerate

  diag_log("pads %d -> %d, re-enumerating\n", g_pads_active, want);
  if (tud_connected()) {
    tud_disconnect();
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - t0 < 60) { tud_task(); diag_supervise(to_ms_since_boot(get_absolute_time())); }
  }
  g_pads_active = want;
  pending = -1;
  if (want > 0) tud_connect();
}

int main(void) {
  set_sys_clock_khz(120000, true);     // PIO-USB needs a multiple of 12 MHz

#if GC3_VBUS_EN_PIN >= 0
  gpio_init(GC3_VBUS_EN_PIN);
  gpio_set_dir(GC3_VBUS_EN_PIN, GPIO_OUT);
  gpio_put(GC3_VBUS_EN_PIN, 1);
#endif

  diag_init();
  settings_load();
  g_usb_mode = boot_mode_requested();
  boot_mode_arm(g_usb_mode);

  guncon3_init_shared();
  mapper_init();
  led_init();

  multicore_reset_core1();
  multicore_launch_core1(guncon3_host_core1_entry);

  // Plug-order mode exposes only as many pads as guns are connected: start off the bus and
  // let pad_manager() connect once guns have had a moment to enumerate.
  bool dynamic_pads = (g_usb_mode == BOOT_XINPUT && g_settings.player_mode == PLAYERS_BY_ORDER);
  g_pads_active = dynamic_pads ? 0 : g_settings.pad_count;

  tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  if (dynamic_pads) tud_disconnect();

  if (g_usb_mode == BOOT_WEB) {
    net_init();
    led_set_base(LED_BLUE, true);
  }

  uint32_t last_map = 0, last_btn = 0, bootsel_since = 0;
  int reboot_req = -1;
  uint32_t reboot_at = 0;

  while (true) {
    tud_task();
    uint32_t now = to_ms_since_boot(get_absolute_time());
    diag_supervise(now);

    if (g_usb_mode == BOOT_WEB) net_task();

    if (now != last_map) {
      last_map = now;
      mapper_update(now);

      bool any = false;
      for (int i = 0; i < MAX_GUNS; i++) any |= mapper_pad(i)->connected;

      if (g_usb_mode == BOOT_XINPUT) {
        if (dynamic_pads) pad_manager(now);
        for (int i = 0; i < g_pads_active; i++) {
          if (g_settings.pad_type == PAD_DS4) ds4_send((uint8_t) i, &mapper_pad(i)->rep, now);
          else xinput_send(i, &mapper_pad(i)->rep, now);
        }
        led_set_base(any ? LED_GREEN : LED_AMBER, !any);
        if (mapper_take_webmode_request()) reboot_into(BOOT_WEB);
      } else {
        mapper_take_webmode_request();   // already there
      }
      led_task(now);
    }

    // Hold BOOTSEL for 1 s: switch between XInput and web config mode
    if (now - last_btn >= 200) {
      last_btn = now;
      if (bootsel_pressed()) {
        if (!bootsel_since) bootsel_since = now;
        else if (now - bootsel_since >= 1000) reboot_into(g_usb_mode == BOOT_WEB ? BOOT_XINPUT : BOOT_WEB);
      } else {
        bootsel_since = 0;
      }
    }

    // Reboot requested by the web page: give the HTTP reply time to go out first
    if (reboot_req < 0) {
      int r = http_take_reboot_request();
      if (r >= 0) { reboot_req = r; reboot_at = now + 300; }
    } else if ((int32_t)(now - reboot_at) >= 0) {
      if (reboot_req == 2) reboot_bootsel();
      reboot_into(reboot_req == 1 ? BOOT_WEB : BOOT_XINPUT);
    }
  }
}
