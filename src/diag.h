// Crash / hang diagnostics that survive a reboot, plus a TinyUSB host log ring buffer.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum { RESET_POWER = 0, RESET_REQUESTED, RESET_WATCHDOG, RESET_CORE1_HANG, RESET_FAULT, RESET_PANIC };

void diag_init(void);                 // core0, very early: classify the last reset
void diag_core1_heartbeat(void);      // core1: call every loop
void diag_supervise(uint32_t now_ms); // core0: kick watchdog, reboot if core1 stops
void diag_note_reboot_requested(void);

int  diag_printf(const char *fmt, ...);   // TinyUSB CFG_TUSB_DEBUG_PRINTF target
void diag_log(const char *fmt, ...);      // our own messages
void diag_panic(const char *fmt, ...) __attribute__((noreturn));

// JSON for the config page (writes into buf, returns length)
int  diag_json(char *buf, size_t len);
