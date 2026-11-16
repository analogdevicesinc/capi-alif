/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "parameters.h"
#include "alif_capi_i2c.h"
#include "pinconf.h"

/* The board has 1.82k pull-ups on both lines; the internal ones are on
 * too, as on DK-E8. */
static const struct alif_capi_pin bsp_i2c1_pins[] = {
	{PORT_7, PIN_3, PINMUX_ALTERNATE_FUNCTION_4,
	 PADCTRL_READ_ENABLE | PADCTRL_DRIVER_DISABLED_PULL_UP |
	 PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA},				    /* SCL */
	{PORT_7, PIN_2, PINMUX_ALTERNATE_FUNCTION_4,
	 PADCTRL_READ_ENABLE | PADCTRL_DRIVER_DISABLED_PULL_UP |
	 PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA},				    /* SDA */
};

static const struct alif_capi_i2c_extra bsp_i2c1_extra = {
	bsp_i2c1_pins, sizeof(bsp_i2c1_pins) / sizeof(bsp_i2c1_pins[0])
};

struct capi_i2c_config bsp_i2c_cfg = {
	.ops = &alif_capi_i2c_ops,
	.identifier = CLICK_I2C_ID,
	.initiator = true,
	.extra = (void *)&bsp_i2c1_extra,
};
