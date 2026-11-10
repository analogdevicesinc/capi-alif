/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include CMSIS_device_header
#include "system.h"
#include "capi_time.h"

extern uint32_t alif_get_tick_ms(void);

/**
 * @brief Busy-wait for the given number of SysTick-processor-clock cycles,
 * off the free-running SysTick->VAL down-counter that capi_platform_init()
 * already configured for the 1ms tick. Same proven approach as
 * capi-maxim's maxim_wait_cycles() (chosen there after the Cortex-M4 DWT
 * cycle counter was found not to increment on that hardware) -- used here
 * too rather than the Cortex-M55 DWT counter, to avoid re-deriving/
 * re-verifying that path when this one is already known-good.
 */
static void alif_wait_cycles(uint32_t cycles)
{
	uint32_t load = SysTick->LOAD;
	uint32_t prev = SysTick->VAL;
	uint32_t elapsed = 0;

	while (elapsed < cycles) {
		uint32_t cur = SysTick->VAL;

		if (cur <= prev)
			elapsed += prev - cur;
		else
			/* counter wrapped (reloaded) at least once */
			elapsed += prev + (load - cur) + 1;
		prev = cur;
	}
}

void capi_wait_us_impl(uint32_t us)
{
	uint32_t whole_ms = us / 1000U;
	uint32_t rem_us = us % 1000U;

	if (whole_ms)
		capi_wait_ms_impl(whole_ms);

	if (rem_us)
		alif_wait_cycles((SystemCoreClock / 1000000U) * rem_us);
}

void capi_wait_ms_impl(uint32_t ms)
{
	uint32_t start = alif_get_tick_ms();

	while ((alif_get_tick_ms() - start) < ms)
		;
}

/**
 * @brief Return monotonic uptime in microseconds since capi_platform_init().
 * See capi-maxim's capi_uptime_impl() for why the ms-tick and SysTick->VAL
 * reads need the consistent-read-retry loop (a rollover landing between
 * the two reads can otherwise pair a stale ms with a post-reload VAL).
 */
int capi_uptime_impl(uint64_t *us)
{
	uint32_t ms1, ms2, val;
	uint32_t load = SysTick->LOAD;
	uint32_t cycles_per_us = SystemCoreClock / 1000000U;

	do {
		ms1 = alif_get_tick_ms();
		val = SysTick->VAL;
		ms2 = alif_get_tick_ms();
	} while (ms1 != ms2);

	*us = (uint64_t)ms2 * 1000U + (load - val) / cycles_per_us;

	return 0;
}
