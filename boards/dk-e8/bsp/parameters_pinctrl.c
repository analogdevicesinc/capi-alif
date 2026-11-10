/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "parameters.h"
#include "alif_capi_pinctrl.h"
#include "pinconf.h"

struct capi_pinctrl_config bsp_pinctrl_cfg = {
	.ops = &alif_capi_pinctrl_ops,
};

const struct capi_pinctrl_descriptor bsp_click_uart_pins[BSP_CLICK_UART_PINS] = {
	{ALIF_CAPI_PINCTRL_PIN(PORT_0, PIN_0),
	 CAPI_PIN_INPUT | CAPI_PIN_PULL_UP, PINMUX_ALTERNATE_FUNCTION_2},   /* RX */
	{ALIF_CAPI_PINCTRL_PIN(PORT_0, PIN_1),
	 CAPI_PIN_OUTPUT, PINMUX_ALTERNATE_FUNCTION_2},                     /* TX */
};
