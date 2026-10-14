#pragma once
// BOOT_XINPUT or BOOT_WEB (see settings.h); chosen once at boot, before tud_init
extern int g_usb_mode;
// Pads exposed in the current USB enumeration (controller mode)
extern int g_pads_active;
