/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "alif_capi_pinmux.h"
#include "pinconf.h"
#include <errno.h>

int alif_capi_pinmux_apply(const struct alif_capi_pin *pins, uint8_t n)
{
	if (n && !pins)
		return -EINVAL;
	for (uint8_t i = 0; i < n; i++) {
		if (pinconf_set(pins[i].port, pins[i].pin, pins[i].function,
				pins[i].padctrl) != 0)
			return -EIO;
	}
	return 0;
}
