/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DK-E8 board bsp, shared by both RTSS cores: the console and the mikroBUS
 * Click socket, as Alif's Boards/DevKit-e8 (pins.h, and DevKit-E8.h under Layers)
 * and the DK-E8 user guide (AUGD0023) describe them.
 *
 *   console    UART2, 115200 8N1, through the PRG USB port's J-Link
 *              virtual COM port with SW4 = U2 (SW4 = SE for SETOOLS)
 *   Click SPI  SPI3: SCLK P12_6, MOSI P12_5, MISO P12_4 (alt 2); CS is a
 *              GPIO, P12_7 -- see bsp_click_cs
 *   Click I2C  I2C0: SCL P3_4, SDA P3_5 (alt 5)
 *   Click INT  P8_1, Click RST P8_3 (GPIOs)
 *   Click UART UART0: RX P0_0, TX P0_1 (alt 2), as capi_pinctrl
 *              descriptors in bsp_click_uart_pins
 *
 * JP7 sets the socket's I/O voltage (factory 3-4: 3.3 V).
 *
 * Ports and pads are claimed when used: the SPI/I2C drivers mux their pads
 * at init, and a GPIO pin is muxed on its first direction/value call.
 */
#ifndef BOARD_PARAMETERS_H
#define BOARD_PARAMETERS_H

#include "capi_uart.h"
#include "capi_spi.h"
#include "capi_i2c.h"
#include "capi_gpio.h"
#include "capi_pinctrl.h"

#define CONSOLE_UART_ID		2
#define CONSOLE_BAUD		115200

#define CLICK_SPI_ID		3
#define CLICK_I2C_ID		0
#define CLICK_UART_ID		0

#define CLICK_CS_PORT		12
#define CLICK_CS_PIN		7
#define CLICK_CTL_PORT		8
#define CLICK_INT_PIN		1
#define CLICK_RST_PIN		3

extern struct capi_uart_config bsp_uart_cfg;
extern struct capi_spi_config bsp_spi_cfg;
extern struct capi_i2c_config bsp_i2c_cfg;

/* GPIO port of the Click CS (port 12) and of INT/RST (port 8). */
extern struct capi_gpio_port_config bsp_click_cs_port_cfg;
extern struct capi_gpio_port_config bsp_click_ctl_port_cfg;

/*
 * .port_handle is set by the application after capi_gpio_port_init().
 * CS on P12_7 is inferred from DK-E7, whose Click socket uses the same
 * pads and instances in every other respect (P12_7 is not in DevKit-e8's
 * pins.h); confirm it with examples/pad_check before relying on it.
 */
extern struct capi_gpio_pin bsp_click_cs;
extern struct capi_gpio_pin bsp_click_int;
extern struct capi_gpio_pin bsp_click_rst;

/* capi_pinctrl (CONFIG_PINCTRL): the platform pin controller, and the
 * Click UART pads. */
extern struct capi_pinctrl_config bsp_pinctrl_cfg;
#define BSP_CLICK_UART_PINS	2
extern const struct capi_pinctrl_descriptor bsp_click_uart_pins[BSP_CLICK_UART_PINS];

#endif /* BOARD_PARAMETERS_H */
