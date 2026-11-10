/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * A pad and the function it is muxed to, as a bus driver's `extra` lists
 * them. Explicit per application, not a board default: DK-E7's own pin
 * table (Boards/DevKit-e7/pins.h) muxes no SPI at all and gives its I2C
 * pads to on-board peripherals, so which pads a bus uses is a wiring fact
 * only the application knows.
 */
#ifndef ALIF_CAPI_PINMUX_H
#define ALIF_CAPI_PINMUX_H

#include <stdint.h>

struct alif_capi_pin {
	uint8_t port;		/**< PORT_n */
	uint8_t pin;		/**< PIN_n */
	uint8_t function;	/**< PINMUX_ALTERNATE_FUNCTION_n, per pins.h */
	uint8_t padctrl;	/**< PADCTRL_* flags; inputs need PADCTRL_READ_ENABLE */
};

/** Mux every pad in @p pins. Returns 0 or the first pinconf_set() error. */
int alif_capi_pinmux_apply(const struct alif_capi_pin *pins, uint8_t n);

#endif /* ALIF_CAPI_PINMUX_H */
