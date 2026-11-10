/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ALIF_CAPI_SPI_H
#define ALIF_CAPI_SPI_H

#include "capi_spi.h"
#include "alif_capi_pinmux.h"

/**
 * capi_spi_config.identifier is the controller instance: 0..3 for SPI0..3.
 * Each needs RTE_SPI<n> in the core's RTE_Device.h.
 */
extern const struct capi_spi_ops alif_capi_spi_ops;

/** capi_spi_config.extra: the pads this controller uses (SCLK, MOSI, MISO). */
struct alif_capi_spi_extra {
	const struct alif_capi_pin *pins;
	uint8_t n_pins;
};

#endif /* ALIF_CAPI_SPI_H */
