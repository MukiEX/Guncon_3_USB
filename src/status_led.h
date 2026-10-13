#pragma once
#include <stdint.h>
#include <stdbool.h>

#define LED_RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define LED_OFF    LED_RGB(0, 0, 0)
#define LED_WHITE  LED_RGB(60, 60, 60)
#define LED_BLUE   LED_RGB(0, 0, 60)
#define LED_GREEN  LED_RGB(0, 50, 0)
#define LED_AMBER  LED_RGB(50, 25, 0)

void led_init(void);
void led_set_base(uint32_t rgb, bool breathe);   // steady state colour
void led_flash(int count, uint32_t rgb);         // blink `count` times, then back to base
void led_task(uint32_t now_ms);
