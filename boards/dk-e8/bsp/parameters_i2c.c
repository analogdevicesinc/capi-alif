/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "parameters.h"
#include "alif_capi_i2c.h"
#include "pinconf.h"

/* Already muxed by Alif's pin table (pins.h, "Clickboard"); listed so the
 * bus does not depend on it. The Click provides the pull-ups. */
static const struct alif_capi_pin bsp_i2c0_pins[] = {
	{PORT_3, PIN_4, PINMUX_ALTERNATE_FUNCTION_5,
	 PADCTRL_READ_ENABLE | PADCTRL_DRIVER_DISABLED_PULL_UP |
	 PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA},				    /* SCL */
	{PORT_3, PIN_5, PINMUX_ALTERNATE_FUNCTION_5,
	 PADCTRL_READ_ENABLE | PADCTRL_DRIVER_DISABLED_PULL_UP |
	 PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA},				    /* SDA */
};

static const struct alif_capi_i2c_extra bsp_i2c0_extra = {
	bsp_i2c0_pins, sizeof(bsp_i2c0_pins) / sizeof(bsp_i2c0_pins[0])
};

struct capi_i2c_config bsp_i2c_cfg = {
	.ops = &alif_capi_i2c_ops,
	.identifier = CLICK_I2C_ID,
	.initiator = true,
	.extra = (void *)&bsp_i2c0_extra,
};
