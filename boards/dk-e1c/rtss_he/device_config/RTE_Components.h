/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal stand-in for the file CMSIS-Toolbox normally auto-generates from
 * a .cproject.yml's component list. capi-alif doesn't use CMSIS-Toolbox --
 * CMSIS_device_header is set via a compiler -D (see
 * sdk/alif_platform_sdk.cmake's ALIF_SDK_COMPILE_DEFS) so the #define below
 * is only a fallback for any DFP source that #includes this file directly
 * without checking whether the macro is already defined.
 */
#ifndef RTE_COMPONENTS_H
#define RTE_COMPONENTS_H

#ifndef CMSIS_device_header
#define CMSIS_device_header "soc.h"
#endif

/* Component-enable flags: Alif_CMSIS's Driver_USART.c/Driver_GPIO.c wrap
 * their ENTIRE content in #if defined(RTE_Drivers_USART)/RTE_Drivers_GPIO
 * (normally set here by CMSIS-Toolbox's component resolution) -- without
 * these, RTE_UART4=1 etc. in RTE_Device.h has no effect and the driver
 * source compiles to an empty translation unit (confirmed: silently
 * produced a near-empty .o with zero symbols, only caught at final link
 * as "undefined reference to Driver_USART4"). */
#define RTE_Drivers_USART 1
#define RTE_Drivers_GPIO  1
/* Same for the bus drivers capi-alif wraps (alif_capi_spi.c, alif_capi_i2c.c);
 * Driver_I2C.c itself has no such guard. Harmless when a driver is not
 * configured in: its source is then not compiled at all. */
#define RTE_Drivers_SPI     1
#define RTE_Drivers_I3C     1

#endif /* RTE_COMPONENTS_H */
