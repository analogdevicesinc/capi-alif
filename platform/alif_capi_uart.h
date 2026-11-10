/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ALIF_CAPI_UART_H
#define ALIF_CAPI_UART_H

#include "capi_uart.h"

extern const struct capi_uart_ops alif_capi_uart_ops;

int alif_capi_uart_transmit(struct capi_uart_handle *handle, const uint8_t *buf, uint32_t len);

int alif_uart_stdio_enable(struct capi_uart_handle *handle);

#endif /* ALIF_CAPI_UART_H */
