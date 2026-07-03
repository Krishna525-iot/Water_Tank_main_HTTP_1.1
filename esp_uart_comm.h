// esp_uart_comm.h  <-- replace your old header with this
#pragma once
#include <Arduino.h>

#define ESP_UART_RX_BUFFER_SIZE 128
#define ESP_UART_BAUD_RATE      115200
#define ESP_UART_DELIMITER      '#'
#define ESP_UART_ENABLE_DEBUG   0

void esp_uart_init();
void esp_uart_send(const char *message);

// NOTE: added timeoutMs parameter (ms). Default 0 = non-blocking.
// Matches implementation in esp_uart_comm.cpp
bool esp_uart_receive(char *buffer, size_t bufferSize, unsigned long timeoutMs = 0);

void esp_uart_processCommand(const char *command);
