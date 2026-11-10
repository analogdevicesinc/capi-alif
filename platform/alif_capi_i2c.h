/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ALIF_CAPI_I2C_H
#define ALIF_CAPI_I2C_H

#include "capi_i2c.h"
#include "alif_capi_pinmux.h"

/**
 * capi_i2c_config.identifier selects the controller:
 *   0..3  I2C0..3 (RTE_I2C<n> in the core's RTE_Device.h)
 *   4     the I3C controller as a plain I2C controller (RTE_I3C),
 *         which is where DK-E7's on-board IMUs are wired
 */
#define ALIF_CAPI_I2C_ON_I3C	4

extern const struct capi_i2c_ops alif_capi_i2c_ops;

/** capi_i2c_config.extra: the pads this controller uses (SCL, SDA). */
struct alif_capi_i2c_extra {
	const struct alif_capi_pin *pins;
	uint8_t n_pins;
};

#endif /* ALIF_CAPI_I2C_H */
