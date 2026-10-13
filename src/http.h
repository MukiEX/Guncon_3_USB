#pragma once
#include <stdbool.h>
void http_init(void);
// Set by the API when the page asks for a reboot; main loop acts on it (-1 = none)
int http_take_reboot_request(void);
