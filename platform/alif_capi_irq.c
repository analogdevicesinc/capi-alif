/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Generic NVIC-backed capi_irq.h implementation for RTSS-HP/RTSS-HE
 * (Cortex-M55, Armv8.1-M NVIC -- same CMSIS-core NVIC_* API on both cores;
 * only the IRQn_Type enum content differs per core's vectors.h, never this
 * file's logic).
 *
 * capi_irq_connect() only records the callback in irq_table[]; nothing
 * dispatches through it. alif_capi_mailbox.c's MHU ISRs
 * are wired directly by defining the DFP's own named vector-table symbols
 * (e.g. MHU_M55HE_M55HP_0_RX_IRQHandler) as strong overrides, not through
 * this table -- see that file.
 */

#include <string.h>
#include <errno.h>
#include CMSIS_device_header
#include "capi_irq.h"

/* AE722F80F55D5 RTSS-HP/RTSS-HE: highest defined external IRQ is 63 (see
 * vectors.h) -- 64 covers the full external IRQ range on both cores. */
#define ALIF_CAPI_IRQ_MAX_IRQS 64

struct alif_capi_irq_entry {
	capi_isr_callback_t callback;
	void *arg;
	bool enabled;
};

static struct alif_capi_irq_entry irq_table[ALIF_CAPI_IRQ_MAX_IRQS];
static bool irq_initialized;

static inline bool is_valid_irq(uint32_t irq)
{
	return irq < ALIF_CAPI_IRQ_MAX_IRQS;
}

int capi_irq_init(struct capi_irq_config *config)
{
	(void)config;

	if (irq_initialized)
		return 0;

	memset(irq_table, 0, sizeof(irq_table));
	irq_initialized = true;

	return 0;
}

int capi_irq_deinit(void)
{
	uint32_t i;

	if (!irq_initialized)
		return 0;

	for (i = 0; i < ALIF_CAPI_IRQ_MAX_IRQS; i++) {
		if (irq_table[i].enabled) {
			NVIC_DisableIRQ((IRQn_Type)i);
			irq_table[i].enabled = false;
		}
		irq_table[i].callback = NULL;
		irq_table[i].arg = NULL;
	}

	irq_initialized = false;

	return 0;
}

int capi_irq_global_enable(void)
{
	__enable_irq();
	return 0;
}

int capi_irq_global_disable(void)
{
	__disable_irq();
	return 0;
}

int capi_irq_enable(uint32_t irq)
{
	if (!irq_initialized)
		return -ENODEV;
	if (!is_valid_irq(irq))
		return -EINVAL;

	NVIC_EnableIRQ((IRQn_Type)irq);
	irq_table[irq].enabled = true;

	return 0;
}

int capi_irq_disable(uint32_t irq)
{
	if (!irq_initialized)
		return -ENODEV;
	if (!is_valid_irq(irq))
		return -EINVAL;

	NVIC_DisableIRQ((IRQn_Type)irq);
	irq_table[irq].enabled = false;

	return 0;
}

int capi_irq_connect(uint32_t irq, capi_isr_callback_t isr, void *arg)
{
	if (!irq_initialized)
		return -ENODEV;
	if (!is_valid_irq(irq) || !isr)
		return -EINVAL;

	irq_table[irq].callback = isr;
	irq_table[irq].arg = arg;

	return 0;
}

int capi_irq_clear_pending(uint32_t irq)
{
	if (!irq_initialized)
		return -ENODEV;
	if (!is_valid_irq(irq))
		return -EINVAL;

	NVIC_ClearPendingIRQ((IRQn_Type)irq);

	return 0;
}

int capi_irq_get_status(uint32_t irq, uint32_t *pactive)
{
	if (!irq_initialized)
		return -ENODEV;
	if (!is_valid_irq(irq) || !pactive)
		return -EINVAL;

	*pactive = NVIC_GetPendingIRQ((IRQn_Type)irq) || NVIC_GetActive((IRQn_Type)irq);

	return 0;
}

int capi_irq_set_priority(uint32_t irq, uint32_t priority)
{
	if (!irq_initialized)
		return -ENODEV;
	if (!is_valid_irq(irq))
		return -EINVAL;

	NVIC_SetPriority((IRQn_Type)irq, priority);

	return 0;
}

int capi_irq_get_priority(uint32_t irq, uint32_t *priority)
{
	if (!irq_initialized)
		return -ENODEV;
	if (!is_valid_irq(irq) || !priority)
		return -EINVAL;

	*priority = NVIC_GetPriority((IRQn_Type)irq);

	return 0;
}

/**
 * @brief Not supported: this SoC's IRQs are fixed level/edge per source at
 * the hardware level (no generic NVIC-level trigger-type register on
 * Armv8-M external IRQs); pin-level edge/level selection for GPIO belongs
 * to a (not yet implemented) gpio_irq driver, not this generic NVIC layer.
 */
int capi_irq_set_level_edge_trigger(uint32_t irq, enum capi_irq_trig_level trigger)
{
	(void)irq;
	(void)trigger;

	return -ENOTSUP;
}

/**
 * @brief Dispatch a fired IRQ to its registered callback, for any future
 * driver that wires a real vector to it (see file header -- none does yet).
 */
void alif_irq_dispatch(uint32_t irq)
{
	const struct alif_capi_irq_entry *entry;

	if (!is_valid_irq(irq))
		return;

	entry = &irq_table[irq];
	if (entry->callback)
		entry->callback(entry->arg);
}
