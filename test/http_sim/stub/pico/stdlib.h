#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
typedef uint64_t absolute_time_t;
static inline absolute_time_t get_absolute_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static inline uint32_t to_ms_since_boot(absolute_time_t t){return (uint32_t)(t/1000);}
#define tight_loop_contents()
