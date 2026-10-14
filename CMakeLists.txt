cmake_minimum_required(VERSION 3.13)

# Default target: Waveshare RP2350-USB-A. Override with -DPICO_BOARD=pico2 / pico etc.
if (NOT PICO_BOARD)
  set(PICO_BOARD waveshare_rp2350_usb_a)
endif()

# TinyUSB 0.21 (newer than the SDK's bundled copy; better hub handling) and Pico-PIO-USB live in lib/
set(PICO_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb)
set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/Pico-PIO-USB)

include(pico_sdk_import.cmake)
project(gc3_xinput C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(TINYUSB ${PICO_TINYUSB_PATH})
set(LWIP ${PICO_SDK_PATH}/lib/lwip)

add_executable(gc3_xinput
  src/main.c
  src/guncon3.c
  src/guncon3_decode.c
  src/mapper.c
  src/settings.c
  src/diag.c
  src/xinput_dev.c
  src/usb_descriptors.c
  src/status_led.c
  src/net.c
  src/http.c
  ${TINYUSB}/lib/networking/dhserver.c
  ${TINYUSB}/lib/networking/dnserver.c
  ${TINYUSB}/lib/networking/rndis_reports.c
  ${LWIP}/src/core/def.c
  ${LWIP}/src/core/inet_chksum.c
  ${LWIP}/src/core/init.c
  ${LWIP}/src/core/ip.c
  ${LWIP}/src/core/mem.c
  ${LWIP}/src/core/memp.c
  ${LWIP}/src/core/netif.c
  ${LWIP}/src/core/pbuf.c
  ${LWIP}/src/core/raw.c
  ${LWIP}/src/core/stats.c
  ${LWIP}/src/core/sys.c
  ${LWIP}/src/core/tcp.c
  ${LWIP}/src/core/tcp_in.c
  ${LWIP}/src/core/tcp_out.c
  ${LWIP}/src/core/timeouts.c
  ${LWIP}/src/core/udp.c
  ${LWIP}/src/core/ipv4/etharp.c
  ${LWIP}/src/core/ipv4/icmp.c
  ${LWIP}/src/core/ipv4/ip4.c
  ${LWIP}/src/core/ipv4/ip4_addr.c
  ${LWIP}/src/core/ipv4/ip4_frag.c
  ${LWIP}/src/netif/ethernet.c
)

# Embedded web page
set(WEBPAGE_H ${CMAKE_CURRENT_BINARY_DIR}/generated/webpage.h)
add_custom_command(
  OUTPUT ${WEBPAGE_H}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
  COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/tools/embed_web.py ${CMAKE_CURRENT_LIST_DIR}/web/index.html ${WEBPAGE_H}
  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/web/index.html ${CMAKE_CURRENT_LIST_DIR}/tools/embed_web.py
)
add_custom_target(gc3_webpage DEPENDS ${WEBPAGE_H})
add_dependencies(gc3_xinput gc3_webpage)

pico_generate_pio_header(gc3_xinput ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_include_directories(gc3_xinput PRIVATE
  src
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${TINYUSB}/lib/networking
  ${LWIP}/src/include
)

target_compile_options(gc3_xinput PRIVATE -Wall -Wno-unused-function)
target_compile_definitions(gc3_xinput PRIVATE PICO_PANIC_FUNCTION=diag_panic)
if (GC3_PIO_USB_DP_PIN)
  target_compile_definitions(gc3_xinput PRIVATE GC3_PIO_USB_DP_PIN=${GC3_PIO_USB_DP_PIN})
endif()

target_link_libraries(gc3_xinput PRIVATE
  pico_stdlib
  pico_multicore
  pico_flash
  pico_unique_id
  hardware_pio
  hardware_flash
  hardware_watchdog
  tinyusb_device
  tinyusb_host
  tinyusb_pico_pio_usb
)

pico_set_program_name(gc3_xinput "GunCon 3 XInput Adapter")
pico_add_extra_outputs(gc3_xinput)
