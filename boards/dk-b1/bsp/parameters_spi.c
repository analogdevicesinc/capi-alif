/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "parameters.h"
#include "alif_capi_spi.h"
#include "pinconf.h"

/* The socket's level translator loads the outputs, so they get fast edges
 * and full drive, and MISO its Schmitt trigger, as on DK-E8. */
#define BSP_SPI_OUT_PADCTRL	(PADCTRL_SLEW_RATE_FAST | PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA)
static const struct alif_capi_pin bsp_spi1_pins[] = {
	{PORT_8, PIN_4, PINMUX_ALTERNATE_FUNCTION_3,
	 PADCTRL_READ_ENABLE | PADCTRL_SCHMITT_TRIGGER_ENABLE},	   /* MISO */
	{PORT_8, PIN_5, PINMUX_ALTERNATE_FUNCTION_3, BSP_SPI_OUT_PADCTRL}, /* MOSI */
	{PORT_8, PIN_6, PINMUX_ALTERNATE_FUNCTION_3, BSP_SPI_OUT_PADCTRL}, /* SCLK */
	{PORT_8, PIN_7, PINMUX_ALTERNATE_FUNCTION_3, BSP_SPI_OUT_PADCTRL}, /* SS0 */
};

static const struct alif_capi_spi_extra bsp_spi1_extra = {
	bsp_spi1_pins, sizeof(bsp_spi1_pins) / sizeof(bsp_spi1_pins[0])
};

struct capi_spi_config bsp_spi_cfg = {
	.ops = &alif_capi_spi_ops,
	.identifier = CLICK_SPI_ID,
	.extra = (void *)&bsp_spi1_extra,
};
