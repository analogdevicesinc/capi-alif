/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The Click socket's GPIOs: CS (P12_7), INT and RST (both on port 8).
 */
#include "parameters.h"
#include "alif_capi_gpio.h"

struct capi_gpio_port_config bsp_click_cs_port_cfg = {
	.ops = &alif_capi_gpio_ops,
	.identifier = CLICK_CS_PORT,
	.num_pins = 8,
};

struct capi_gpio_port_config bsp_click_ctl_port_cfg = {
	.ops = &alif_capi_gpio_ops,
	.identifier = CLICK_CTL_PORT,
	.num_pins = 8,
};

struct capi_gpio_pin bsp_click_cs = { .number = CLICK_CS_PIN };
struct capi_gpio_pin bsp_click_int = { .number = CLICK_INT_PIN };
struct capi_gpio_pin bsp_click_rst = { .number = CLICK_RST_PIN };
