/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DK-B1 GPIO table for the DFP's board_config.c. Empty: no pin is set up
 * as a GPIO at init, and capi-alif does not call board_gpios_config().
 */
#ifndef __GPIOS_H__
#define __GPIOS_H__

#include <stdint.h>
#include "pinconf.h"

struct gpioconf {
	uint8_t port_num;
	uint8_t hardware_mode_lptimer_on_lpgpio;
	uint8_t initial_state_high;
	uint8_t direction_output;
	uint8_t debounce_enable;
	uint8_t interrupt_enable;
	uint8_t interrupt_edge;
	uint8_t interrupt_polarity_high_edge_rising;
	uint8_t interrupt_both_edge;
};

const struct gpioconf board_gpioconf[] = {};

#endif /* __GPIOS_H__ */
