/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * capi_pinctrl over the Alif DFP's pinconf (drivers/source/pinconf.c):
 * each capi_pinctrl_descriptor becomes one pinconf_set(port, pin,
 * alternate function, pad control) call.
 *
 * Descriptor fields:
 *   number        ALIF_CAPI_PINCTRL_PIN(PORT_n, PIN_m) -- port 0 up to the
 *                 SoC's last GPIO port (17 on the E8; 15 is LPGPIO), pin 0..7
 *   pinmux_flags  the pad's alternate function, 0..7
 *                 (PINMUX_ALTERNATE_FUNCTION_n; pins.h lists each pad's)
 *   pn_flags      generic CAPI_PIN_* flags:
 *                   CAPI_PIN_INPUT      -> PADCTRL_READ_ENABLE
 *                   CAPI_PIN_PULL_UP    -> PADCTRL_DRIVER_DISABLED_PULL_UP
 *                   CAPI_PIN_PULL_DOWN  -> PADCTRL_DRIVER_DISABLED_PULL_DOWN
 *                   CAPI_PIN_OPEN_DRAIN -> PADCTRL_DRIVER_OPEN_DRAIN
 *                   CAPI_PIN_OUTPUT, CAPI_PIN_ENABLE -> nothing (the output
 *                   driver is on unless open drain)
 *                 OR'd with ALIF_CAPI_PADCTRL(PADCTRL_*) for the settings
 *                 that have no generic flag (Schmitt trigger, slew rate,
 *                 drive strength). A raw pull setting together with a
 *                 generic pull flag is refused.
 *
 * Deinit releases the handle only; pins keep their last setting (the DFP
 * has no per-pin reset default to return to).
 */
#ifndef ALIF_CAPI_PINCTRL_H
#define ALIF_CAPI_PINCTRL_H

#include "capi_pinctrl.h"

#define ALIF_CAPI_PINCTRL_PIN(port, pin)	((int)(((port) << 3) | ((pin) & 0x7)))
#define ALIF_CAPI_PINCTRL_PORT(number)		((number) >> 3)
#define ALIF_CAPI_PINCTRL_PAD(number)		((uint8_t)((number) & 0x7))

/** Raw Alif PADCTRL_* bits carried in pn_flags. */
#define ALIF_CAPI_PADCTRL(padctrl)		((uint32_t)(padctrl) << 8)

extern const struct capi_pinctrl_ops alif_capi_pinctrl_ops;

/** Pad control byte for a descriptor's pn_flags, or -EINVAL for flags
 *  that contradict each other (pull-up and pull-down together, or a raw
 *  pull setting combined with a generic pull flag). */
int alif_capi_pinctrl_padctrl(uint32_t pn_flags);

#endif /* ALIF_CAPI_PINCTRL_H */
