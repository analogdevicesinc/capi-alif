/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include CMSIS_device_header
#include "system.h"
#include "board_config.h"
#include "capi_platform.h"
#include "capi_uart.h"

static volatile uint32_t alif_tick_ms;

void SysTick_Handler(void)
{
	capi_platform_tick();
}

/**
 * @brief One-call platform init. SystemInit() already ran from startup.c
 * before main(); this applies the board's pin muxing and brings up a 1ms
 * SysTick for capi_wait_ms()/capi_uptime().
 *
 * board_pins_config() is not optional: peripherals come up fine without it
 * but their signals never reach the package pins, so e.g. UART output is
 * silently dropped with no error reported anywhere. Every DFP example
 * calls it before producing any output.
 *
 * @return 0, -EIO if the board pin table fails to apply, -EINVAL if the
 * SysTick reload does not fit.
 */
int capi_platform_init(void)
{
	if (board_pins_config())
		return -EIO;

	SystemCoreClockUpdate();
	if (SysTick_Config(SystemCoreClock / 1000U))
		return -EINVAL;

	return 0;
}

int capi_platform_get_clock_hz(uint64_t *clock_hz)
{
	if (!clock_hz)
		return -EINVAL;

	*clock_hz = SystemCoreClock;

	return 0;
}

void capi_platform_tick(void)
{
	alif_tick_ms++;
}

uint32_t alif_get_tick_ms(void)
{
	return alif_tick_ms;
}

int alif_uart_stdio_enable(struct capi_uart_handle *handle);

int capi_stdio_init(struct capi_uart_handle *handle)
{
	return alif_uart_stdio_enable(handle);
}
