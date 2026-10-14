#pragma once
int flash_safe_execute(void (*f)(void*), void *p, unsigned t);
#define PICO_OK 0
