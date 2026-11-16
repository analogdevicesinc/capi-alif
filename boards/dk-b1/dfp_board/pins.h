/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DK-B1 pin table for the DFP's libs/board_config/board_config.c, which
 * board_pins_config() walks at init. The DFP has no Balletto board, so this
 * stands in for its Boards/<board>/pins.h, and holds only the pads every
 * application needs. Other pads are muxed by their driver when used.
 */
#ifndef __PINS_H__
#define __PINS_H__

#include "pinconf.h"

struct pinconf {
	uint8_t port;
	uint8_t pin;
	uint8_t alternate_function;
	uint8_t pad_control;
};

const struct pinconf board_pinconf[] = {
	/* Console: UART2_RX_B on P5_2, UART2_TX_B on P5_3. */
	{PORT_5, PIN_2, PINMUX_ALTERNATE_FUNCTION_3, PADCTRL_READ_ENABLE},
	{PORT_5, PIN_3, PINMUX_ALTERNATE_FUNCTION_2, PADCTRL_OUTPUT_DRIVE_STRENGTH_2MA},
};

#endif /* __PINS_H__ */
