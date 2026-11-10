/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "alif_capi_pinctrl.h"
#include "capi_alloc.h"
#include "pinconf.h"
#include <errno.h>

/* The SoC's last GPIO port (17 on the E8, 14 or 9 on smaller parts), or
 * port 15, the LPGPIO port every Ensemble part has, if that is higher.
 * pinconf_set() still checks the exact pin range per port. */
#if GPIO_MAX_PORT > PORT_15
#define ALIF_PINCTRL_MAX_PORT	GPIO_MAX_PORT
#else
#define ALIF_PINCTRL_MAX_PORT	PORT_15
#endif
#define ALIF_PINCTRL_MAX_FUNC	7

int alif_capi_pinctrl_padctrl(uint32_t pn_flags)
{
	uint32_t padctrl = (pn_flags >> 8) & 0xFF;
	uint32_t raw_pull = padctrl & PADCTRL_DRIVER_DISABLED_BUS_REPEATER;

	if ((pn_flags & CAPI_PIN_PULL_UP) && (pn_flags & CAPI_PIN_PULL_DOWN))
		return -EINVAL;
	/* The pull field is two bits: a generic pull on top of a raw one would
	 * OR into a different mode (pull-up | pull-down = bus repeater). */
	if (raw_pull && (pn_flags & (CAPI_PIN_PULL_UP | CAPI_PIN_PULL_DOWN)))
		return -EINVAL;

	if (pn_flags & CAPI_PIN_INPUT)
		padctrl |= PADCTRL_READ_ENABLE;
	if (pn_flags & CAPI_PIN_PULL_UP)
		padctrl |= PADCTRL_DRIVER_DISABLED_PULL_UP;
	if (pn_flags & CAPI_PIN_PULL_DOWN)
		padctrl |= PADCTRL_DRIVER_DISABLED_PULL_DOWN;
	if (pn_flags & CAPI_PIN_OPEN_DRAIN)
		padctrl |= PADCTRL_DRIVER_OPEN_DRAIN;

	return (int)padctrl;
}

static int alif_capi_pinctrl_init(struct capi_pinctrl_handle **handle,
				  const struct capi_pinctrl_config *config)
{
	struct capi_pinctrl_handle *h;

	if (!handle || !config)
		return -EINVAL;

	h = *handle;
	if (!h) {
		h = capi_calloc(1, sizeof(*h));
		if (!h)
			return -ENOMEM;
		h->init_allocated = true;
	} else {
		h->init_allocated = false;
	}
	h->ops = config->ops;
	*handle = h;

	return 0;
}

static int alif_capi_pinctrl_deinit(struct capi_pinctrl_handle **handle)
{
	if (!handle || !*handle)
		return -EINVAL;

	if ((*handle)->init_allocated) {
		capi_free(*handle);
		*handle = NULL;
	}

	return 0;
}

static int alif_capi_pinctrl_set_pin(const struct capi_pinctrl_handle *handle,
				     const struct capi_pinctrl_descriptor *pin)
{
	int padctrl;

	if (!handle || !pin || pin->number < 0 ||
	    ALIF_CAPI_PINCTRL_PORT(pin->number) > ALIF_PINCTRL_MAX_PORT ||
	    pin->pinmux_flags > ALIF_PINCTRL_MAX_FUNC)
		return -EINVAL;

	padctrl = alif_capi_pinctrl_padctrl(pin->pn_flags);
	if (padctrl < 0)
		return padctrl;

	if (pinconf_set((uint8_t)ALIF_CAPI_PINCTRL_PORT(pin->number),
			ALIF_CAPI_PINCTRL_PAD(pin->number),
			(uint8_t)pin->pinmux_flags, (uint8_t)padctrl) != 0)
		return -EIO;

	return 0;
}

static int alif_capi_pinctrl_set_pins(const struct capi_pinctrl_handle *handle,
				      const struct capi_pinctrl_descriptor *pins,
				      uint32_t num_pins)
{
	int ret;

	if (num_pins && !pins)
		return -EINVAL;

	for (uint32_t i = 0; i < num_pins; i++) {
		ret = alif_capi_pinctrl_set_pin(handle, &pins[i]);
		if (ret)
			return ret;
	}

	return 0;
}

const struct capi_pinctrl_ops alif_capi_pinctrl_ops = {
	.pinctrl_init = alif_capi_pinctrl_init,
	.pinctrl_deinit = alif_capi_pinctrl_deinit,
	.pinctrl_set_pin = alif_capi_pinctrl_set_pin,
	.pinctrl_set_pins = alif_capi_pinctrl_set_pins,
};
