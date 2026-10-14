#pragma once

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_OS           OPT_OS_PICO
#define CFG_TUSB_DEBUG        0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))

//------------- Device: native USB (rhport 0) -------------//
#define CFG_TUD_ENABLED       1
#define BOARD_TUD_RHPORT      0
#define CFG_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64

// Network class for the web config personality (RNDIS + CDC-ECM)
#define CFG_TUD_ECM_RNDIS     1
#define CFG_TUD_NCM           0
#define CFG_TUD_CDC           0
#define CFG_TUD_MSC           0
#define CFG_TUD_HID           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_VENDOR        0
// XInput is an application class driver (xinput_dev.c)

//------------- Host: PIO-USB (rhport 1) -------------//
#define CFG_TUH_ENABLED       1
#define CFG_TUH_RPI_PIO_USB   1
#define BOARD_TUH_RHPORT      1
#define CFG_TUH_MAX_SPEED     OPT_MODE_FULL_SPEED

#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB           4          // docks often contain 2-3 chained hubs internally
#define CFG_TUH_DEVICE_MAX    10         // non-hub devices: 4 guns + whatever else lives in a dock
#define CFG_TUH_ENDPOINT_MAX  8
#define CFG_TUH_HID           0
#define CFG_TUH_CDC           0
#define CFG_TUH_MSC           0
#define CFG_TUH_VENDOR        0
// GunCon 3 is an application class driver (guncon3.c)
