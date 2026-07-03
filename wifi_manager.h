#pragma once
#include <Arduino.h>

void wifi_init(void);
void wifi_loop(void);

/* Password management */
bool         wifi_change_password(const char *newPass);  /* min 8 chars, max 32 */
const char*  wifi_get_ssid(void);
const char*  wifi_get_password(void);

extern char AP_SSID[32];