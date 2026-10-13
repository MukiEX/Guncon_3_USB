#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#define XINPUT_MAX_PADS MAX_GUNS

// Queue a 20-byte XInput report on pad `pad` (sent on change + periodic keep-alive)
bool xinput_send(int pad, const void *report20, uint32_t now_ms);
