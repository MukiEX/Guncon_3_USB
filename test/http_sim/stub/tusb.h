#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define CFG_TUD_NET_MTU 1514
#define TU_ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
bool tud_ready(void); void tud_task(void);
bool tud_network_can_xmit(uint16_t); void tud_network_xmit(void*,uint16_t); void tud_network_recv_renew(void);
