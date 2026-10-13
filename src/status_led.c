#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "config.h"
#include "status_led.h"

#if GC3_WS2812_PIN >= 0 && NUM_PIOS > 2
#include "ws2812.pio.h"
#define LED_PIO pio2
#define HAVE_LED 1
#else
#define HAVE_LED 0
#endif

static uint32_t s_base;
static bool     s_breathe;
static int      s_flash_left;        // half-cycles remaining
static uint32_t s_flash_rgb, s_next;
#if HAVE_LED
static uint32_t s_shown = 0xFFFFFFFF;
static int      s_sm = -1;
#endif

static void put(uint32_t rgb) {
#if HAVE_LED
  if (s_sm < 0 || rgb == s_shown) return;
  s_shown = rgb;
  uint8_t r = rgb >> 16, g = rgb >> 8, b = rgb;
#if GC3_WS2812_RGB_ORDER
  uint32_t w = ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
#else
  uint32_t w = ((uint32_t) g << 16) | ((uint32_t) r << 8) | b;
#endif
  pio_sm_put_blocking(LED_PIO, s_sm, w << 8u);
#else
  (void) rgb;
#endif
}

void led_init(void) {
#if HAVE_LED
  uint offset = pio_add_program(LED_PIO, &ws2812_program);
  s_sm = pio_claim_unused_sm(LED_PIO, true);
  ws2812_program_init(LED_PIO, s_sm, offset, GC3_WS2812_PIN, 800000);
  put(LED_OFF);
#endif
}

void led_set_base(uint32_t rgb, bool breathe) { s_base = rgb; s_breathe = breathe; }

void led_flash(int count, uint32_t rgb) {
  s_flash_left = count * 2;
  s_flash_rgb = rgb;
  s_next = 0;
}

static uint32_t scale(uint32_t rgb, uint32_t k /*0..255*/) {
  uint32_t r = ((rgb >> 16) & 0xFF) * k / 255, g = ((rgb >> 8) & 0xFF) * k / 255, b = (rgb & 0xFF) * k / 255;
  return LED_RGB(r, g, b);
}

void led_task(uint32_t now) {
  if (s_flash_left > 0) {
    if ((int32_t)(now - s_next) >= 0) {
      put((s_flash_left & 1) ? LED_OFF : s_flash_rgb);
      s_flash_left--;
      s_next = now + (s_flash_left == 0 ? 250 : 150);
    }
    return;
  }
  if ((int32_t)(now - s_next) < 0) return;
  if (s_breathe) {
    uint32_t t = (now / 8) % 512;
    uint32_t k = t < 256 ? t : 511 - t;
    put(scale(s_base, 40 + k * 215 / 255));
  } else {
    put(s_base);
  }
}
