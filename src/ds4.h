// DualShock 4 output mode (HID). See ds4.c.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "mapper.h"

#define DS4_MAX_PADS MAX_GUNS
#define DS4_VID 0x054C
#define DS4_PID 0x09CC

extern const uint8_t ds4_report_descriptor[];
extern const uint16_t ds4_report_descriptor_len;

// Translate the mapper's (XInput-shaped) pad state into a 64-byte DS4 input report
void ds4_build(uint8_t pad, const xinput_report_t *x, uint8_t out[64]);
// Send on change, with a 5 ms keep-alive
bool ds4_send(uint8_t pad, const xinput_report_t *x, uint32_t now_ms);
uint16_t ds4_get_feature(uint8_t pad, uint8_t report_id, uint8_t *buf, uint16_t reqlen);
