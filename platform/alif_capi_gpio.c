/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Ops implementation for capi_gpio.h against Alif_CMSIS's single, unified
 * ARM_DRIVER_GPIO instance. Unlike most vendors' per-port CMSIS-GPIO
 * drivers, Alif exposes every pin on every port through one instance
 * (Alif_CMSIS/Source/Driver_GPIO.c), addressed by a flat
 * ARM_GPIO_Pin_t = port_num * 8 + pin_in_port. A capi "port" here is one
 * 8-pin Alif GPIO bank (port_num = config->identifier); pin->number is 0-7
 * within it.
 *
 * Interrupt/edge-triggered pins (pin_set_event_trigger-equivalent) are not
 * implemented: output and plain polled input only.
 */

#include "capi_gpio.h"
#include "capi_alloc.h"
#include "Driver_GPIO.h"
#include <errno.h>

extern ARM_DRIVER_GPIO Driver_GPIO;

#define ALIF_GPIO_PINS_PER_PORT 8

struct alif_gpio_priv {
	uint32_t port_num;
	/* Pins this driver has claimed: muxed to GPIO by Driver_GPIO.Setup(). */
	uint8_t claimed;
};

static inline ARM_GPIO_Pin_t alif_gpio_pin_id(uint32_t port_num, uint32_t local_pin)
{
	return (ARM_GPIO_Pin_t)(port_num * ALIF_GPIO_PINS_PER_PORT + local_pin);
}

/*
 * Claim a pin for GPIO on first use, not at port init. Driver_GPIO.Setup()
 * re-muxes the pad to function 0 (GPIO input, high-Z), so claiming the
 * whole port at init took every other pin on it away from whatever it was
 * muxed to: initialising port 12 for one chip-select disconnected SPI3's
 * MISO on P12_4, and port 1 would take the UART2 console. Seen on DK-E7.
 */
static void alif_gpio_claim(struct alif_gpio_priv *priv, uint32_t pin)
{
	if (priv->claimed & (1U << pin))
		return;
	Driver_GPIO.Setup(alif_gpio_pin_id(priv->port_num, pin), NULL);
	priv->claimed |= (uint8_t)(1U << pin);
}

static int alif_capi_gpio_port_init(struct capi_gpio_port_handle **handle,
				    const struct capi_gpio_port_config *config)
{
	struct capi_gpio_port_handle *gpio_handle;
	struct alif_gpio_priv *priv;

	if (!handle || !config)
		return -EINVAL;

	if (*handle == NULL) {
		gpio_handle = (struct capi_gpio_port_handle *)capi_calloc(1, sizeof(*gpio_handle));
		if (!gpio_handle)
			return -ENOMEM;

		priv = (struct alif_gpio_priv *)capi_calloc(1, sizeof(*priv));
		if (!priv) {
			capi_free(gpio_handle);
			return -ENOMEM;
		}

		gpio_handle->priv = priv;
		gpio_handle->init_allocated = true;
	} else {
		gpio_handle = *handle;
		gpio_handle->init_allocated = false;

		if (!gpio_handle->priv)
			return -EINVAL;

		priv = gpio_handle->priv;
	}

	gpio_handle->ops = config->ops;
	priv->port_num = (uint32_t)config->identifier;
	priv->claimed = 0;	/* pins are claimed as they are used */

	*handle = gpio_handle;

	return 0;
}

static int alif_capi_gpio_port_deinit(struct capi_gpio_port_handle **handle)
{
	struct capi_gpio_port_handle *gpio_handle;

	if (!handle || !*handle)
		return -EINVAL;

	gpio_handle = *handle;

	if (gpio_handle->init_allocated) {
		capi_free(gpio_handle->priv);
		capi_free(gpio_handle);
		*handle = NULL;
	}

	return 0;
}

static int alif_capi_gpio_port_set_direction(struct capi_gpio_port_handle *handle,
					     uint64_t direction_bitmask)
{
	struct alif_gpio_priv *priv;
	uint32_t i;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;

	/* A whole-port direction is a claim on every pin of the port. */
	for (i = 0; i < ALIF_GPIO_PINS_PER_PORT; i++) {
		ARM_GPIO_DIRECTION dir = (direction_bitmask & (1ULL << i)) ?
			ARM_GPIO_INPUT : ARM_GPIO_OUTPUT;

		alif_gpio_claim(priv, i);
		Driver_GPIO.SetDirection(alif_gpio_pin_id(priv->port_num, i), dir);
	}

	return 0;
}

static int alif_capi_gpio_port_get_direction(struct capi_gpio_port_handle *handle,
					     uint64_t *direction_bitmask)
{
	/* Alif_CMSIS's ARM_DRIVER_GPIO exposes no "read back configured
	 * direction" call -- report unknown (all-input, the safe default
	 * reading) rather than guessing. */
	if (!handle || !handle->priv || !direction_bitmask)
		return -EINVAL;

	*direction_bitmask = 0xFFULL;

	return 0;
}

static int alif_capi_gpio_port_set_value(struct capi_gpio_port_handle *handle,
					 uint64_t value_bitmask)
{
	struct alif_gpio_priv *priv;
	uint32_t i;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;

	for (i = 0; i < ALIF_GPIO_PINS_PER_PORT; i++)
		Driver_GPIO.SetOutput(alif_gpio_pin_id(priv->port_num, i),
				      (value_bitmask >> i) & 1U);

	return 0;
}

static int alif_capi_gpio_port_get_value(struct capi_gpio_port_handle *handle,
					 uint64_t *value_bitmask)
{
	struct alif_gpio_priv *priv;
	uint32_t i, val = 0;

	if (!handle || !handle->priv || !value_bitmask)
		return -EINVAL;

	priv = handle->priv;

	for (i = 0; i < ALIF_GPIO_PINS_PER_PORT; i++) {
		if (Driver_GPIO.GetInput(alif_gpio_pin_id(priv->port_num, i)))
			val |= (1U << i);
	}
	*value_bitmask = val;

	return 0;
}

static int alif_capi_gpio_port_set_raw_value(struct capi_gpio_port_handle *handle,
					     uint64_t value_bitmask)
{
	return alif_capi_gpio_port_set_value(handle, value_bitmask);
}

static int alif_capi_gpio_port_get_raw_value(struct capi_gpio_port_handle *handle,
					     uint64_t *value_bitmask)
{
	return alif_capi_gpio_port_get_value(handle, value_bitmask);
}

static int alif_capi_gpio_port_toggle(struct capi_gpio_port_handle *handle,
				      uint64_t pins_bitmask)
{
	struct alif_gpio_priv *priv;
	uint32_t i;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;

	for (i = 0; i < ALIF_GPIO_PINS_PER_PORT; i++) {
		if (pins_bitmask & (1ULL << i)) {
			ARM_GPIO_Pin_t p = alif_gpio_pin_id(priv->port_num, i);

			Driver_GPIO.SetOutput(p, Driver_GPIO.GetInput(p) ? 0U : 1U);
		}
	}

	return 0;
}

static int alif_capi_gpio_pin_set_direction(struct capi_gpio_pin *pin, uint8_t direction)
{
	struct alif_gpio_priv *priv;

	if (!pin || !pin->port_handle || !pin->port_handle->priv)
		return -EINVAL;

	priv = pin->port_handle->priv;
	if (pin->number >= ALIF_GPIO_PINS_PER_PORT)
		return -EINVAL;
	alif_gpio_claim(priv, pin->number);
	Driver_GPIO.SetDirection(alif_gpio_pin_id(priv->port_num, pin->number),
				 direction == CAPI_GPIO_INPUT ? ARM_GPIO_INPUT : ARM_GPIO_OUTPUT);

	return 0;
}

static int alif_capi_gpio_pin_get_direction(struct capi_gpio_pin *pin, uint8_t *direction)
{
	/* See alif_capi_gpio_port_get_direction(): no readback available. */
	if (!pin || !pin->port_handle || !pin->port_handle->priv || !direction)
		return -EINVAL;

	*direction = CAPI_GPIO_INPUT;

	return 0;
}

static int alif_capi_gpio_pin_set_value(struct capi_gpio_pin *pin, uint8_t value)
{
	struct alif_gpio_priv *priv;

	if (!pin || !pin->port_handle || !pin->port_handle->priv)
		return -EINVAL;

	priv = pin->port_handle->priv;
	Driver_GPIO.SetOutput(alif_gpio_pin_id(priv->port_num, pin->number),
			      value == CAPI_GPIO_HIGH ? 1U : 0U);

	return 0;
}

static int alif_capi_gpio_pin_get_value(struct capi_gpio_pin *pin, uint8_t *value)
{
	struct alif_gpio_priv *priv;

	if (!pin || !pin->port_handle || !pin->port_handle->priv || !value)
		return -EINVAL;

	priv = pin->port_handle->priv;
	*value = Driver_GPIO.GetInput(alif_gpio_pin_id(priv->port_num, pin->number)) ?
		CAPI_GPIO_HIGH : CAPI_GPIO_LOW;

	return 0;
}

static int alif_capi_gpio_pin_set_raw_value(struct capi_gpio_pin *pin, uint8_t value)
{
	return alif_capi_gpio_pin_set_value(pin, value);
}

static int alif_capi_gpio_pin_get_raw_value(struct capi_gpio_pin *pin, uint8_t *value)
{
	return alif_capi_gpio_pin_get_value(pin, value);
}

static int alif_capi_gpio_pin_toggle(struct capi_gpio_pin *pin)
{
	struct alif_gpio_priv *priv;
	ARM_GPIO_Pin_t p;

	if (!pin || !pin->port_handle || !pin->port_handle->priv)
		return -EINVAL;

	priv = pin->port_handle->priv;
	p = alif_gpio_pin_id(priv->port_num, pin->number);
	Driver_GPIO.SetOutput(p, Driver_GPIO.GetInput(p) ? 0U : 1U);

	return 0;
}

const struct capi_gpio_ops alif_capi_gpio_ops = {
	.port_init = alif_capi_gpio_port_init,
	.port_deinit = alif_capi_gpio_port_deinit,
	.port_set_direction = alif_capi_gpio_port_set_direction,
	.port_get_direction = alif_capi_gpio_port_get_direction,
	.port_set_value = alif_capi_gpio_port_set_value,
	.port_get_value = alif_capi_gpio_port_get_value,
	.port_set_raw_value = alif_capi_gpio_port_set_raw_value,
	.port_get_raw_value = alif_capi_gpio_port_get_raw_value,
	.port_toggle = alif_capi_gpio_port_toggle,
	.pin_set_direction = alif_capi_gpio_pin_set_direction,
	.pin_get_direction = alif_capi_gpio_pin_get_direction,
	.pin_set_value = alif_capi_gpio_pin_set_value,
	.pin_get_value = alif_capi_gpio_pin_get_value,
	.pin_set_raw_value = alif_capi_gpio_pin_set_raw_value,
	.pin_get_raw_value = alif_capi_gpio_pin_get_raw_value,
	.pin_toggle = alif_capi_gpio_pin_toggle,
};
