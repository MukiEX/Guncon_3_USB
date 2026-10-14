#pragma once
#include <stdint.h>
typedef struct { uint32_t scratch[8]; } wd_t; extern wd_t wdhw; 
#define watchdog_hw (&wdhw)
static inline void watchdog_reboot(int a,int b,int c){(void)a;(void)b;(void)c;}
