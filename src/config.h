// GunCon 3 -> XInput adapter: build-time configuration
#pragma once

#include "pico.h"

#define FW_NAME     "GC3-XInput"
#define FW_VERSION  "0.1.0"

// Up to this many GunCon 3s (and XInput pads). 4 = XInput maximum.
#define MAX_GUNS 4

// ---- PIO-USB host port (D- is always D+ + 1) ------------------------------
#ifndef GC3_PIO_USB_DP_PIN
  #ifdef PICO_DEFAULT_PIO_USB_DP_PIN
    #define GC3_PIO_USB_DP_PIN PICO_DEFAULT_PIO_USB_DP_PIN   // Waveshare RP2350-USB-A: GP12/GP13
  #else
    #define GC3_PIO_USB_DP_PIN 0                             // bare Pico / Pico 2: GP0/GP1
  #endif
#endif

// Some boards need VBUS for the USB-A port switched on with a GPIO. -1 = none.
#ifndef GC3_VBUS_EN_PIN
  #define GC3_VBUS_EN_PIN -1
#endif

// ---- Status LED (WS2812) ---------------------------------------------------
#ifndef GC3_WS2812_PIN
  #ifdef PICO_DEFAULT_WS2812_PIN
    #define GC3_WS2812_PIN PICO_DEFAULT_WS2812_PIN
  #else
    #define GC3_WS2812_PIN -1
  #endif
#endif
// 1 if the LED expects R,G,B byte order instead of the usual G,R,B
#ifndef GC3_WS2812_RGB_ORDER
  #define GC3_WS2812_RGB_ORDER 0
#endif

// ---- USB identity in XInput mode -------------------------------------------
// Microsoft VID with a PID that is *not* in xusb22.inf, so Windows treats the
// device as composite and binds the XInput driver per interface through the
// MS OS 1.0 "XUSB10" compatible ID. Linux xpad binds by VID + interface class.
#ifndef GC3_XINPUT_VID
  #define GC3_XINPUT_VID 0x045E
#endif
#ifndef GC3_XINPUT_PID
  #define GC3_XINPUT_PID 0x7C33
#endif

// Web-config mode identity (RNDIS/ECM network adapter)
#define GC3_WEB_VID 0xCAFE
#define GC3_WEB_PID 0x4C33

// Hotkey timing
#define HOTKEY_WINDOW_MS     60     // C1/C2 must land within this to form the combo
#define HOTKEY_WEBMODE_MS    3000   // hold C1+C2 this long -> reboot into web config
