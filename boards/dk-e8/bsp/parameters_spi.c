/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "parameters.h"
#include "alif_capi_spi.h"
#include "pinconf.h"

/* Alif's default pin table gives P12_4..6 to the CAN transceiver; the SPI
 * driver takes them back at init. The socket's level translators and any
 * extender cable load the outputs, so they get fast edges and full drive
 * (as no-os-alif's DK-E8 SPI setups do), and MISO its Schmitt trigger. */
#define BSP_SPI_OUT_PADCTRL	(PADCTRL_SLEW_RATE_FAST | PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA)
static const struct alif_capi_pin bsp_spi3_pins[] = {
	{PORT_12, PIN_4, PINMUX_ALTERNATE_FUNCTION_2,
	 PADCTRL_READ_ENABLE | PADCTRL_SCHMITT_TRIGGER_ENABLE},	    /* MISO */
	{PORT_12, PIN_5, PINMUX_ALTERNATE_FUNCTION_2, BSP_SPI_OUT_PADCTRL}, /* MOSI */
	{PORT_12, PIN_6, PINMUX_ALTERNATE_FUNCTION_2, BSP_SPI_OUT_PADCTRL}, /* SCLK */
};

static const struct alif_capi_spi_extra bsp_spi3_extra = {
	bsp_spi3_pins, sizeof(bsp_spi3_pins) / sizeof(bsp_spi3_pins[0])
};

struct capi_spi_config bsp_spi_cfg = {
	.ops = &alif_capi_spi_ops,
	.identifier = CLICK_SPI_ID,
	.extra = (void *)&bsp_spi3_extra,
};
