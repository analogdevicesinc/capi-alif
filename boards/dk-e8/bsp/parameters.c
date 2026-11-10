/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The console. The Click GPIOs, the buses and the pin controller are in
 * parameters_{gpio,spi,i2c,pinctrl}.c, which board.cmake adds only when
 * their driver is enabled (CONFIG_GPIO_ALIF, CONFIG_SPI_ALIF,
 * CONFIG_I2C_ALIF, CONFIG_PINCTRL_ALIF), so an application without them
 * does not reference their ops.
 */
#include "parameters.h"
#include "alif_capi_uart.h"

static struct capi_uart_line_config bsp_uart_line_cfg = {
	.baudrate = CONSOLE_BAUD,
};

struct capi_uart_config bsp_uart_cfg = {
	.identifier = CONSOLE_UART_ID,
	.line_config = &bsp_uart_line_cfg,
	.ops = &alif_capi_uart_ops,
};
