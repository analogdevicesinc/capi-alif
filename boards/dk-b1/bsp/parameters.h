/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DK-B1 board bsp: the console and the mikroBUS Click socket, as the DK-B1
 * schematic (220-00315 rev D) wires them.
 *
 *   console    UART2 on P5_2 (RX) / P5_3 (TX), 115200 8N1, through the
 *              J-Link virtual COM port
 *   Click SPI  SPI1: MISO P8_4, MOSI P8_5, SCLK P8_6, CS P8_7 (alt 3).
 *              CS is SS0 for a device with native_cs = 1, or a GPIO --
 *              see bsp_click_cs -- once that is used
 *   Click I2C  I2C1: SDA P7_2, SCL P7_3 (alt 4), shared with the board's
 *              other I2C devices; 1.82k pull-ups on the board
 *   Click INT  P7_7, Click RST P7_6 (GPIOs)
 *   Click UART UART1: RX P8_2, TX P8_3 (alt 2), as capi_pinctrl
 *              descriptors in bsp_click_uart_pins
 *
 * JP9 sets the socket's I/O voltage (factory 3-4: 3.3 V).
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

#define CLICK_SPI_ID		1
#define CLICK_I2C_ID		1
#define CLICK_UART_ID		1

#define CLICK_CS_PORT		8
#define CLICK_CS_PIN		7
#define CLICK_CTL_PORT		7
#define CLICK_INT_PIN		7
#define CLICK_RST_PIN		6

extern struct capi_uart_config bsp_uart_cfg;
extern struct capi_spi_config bsp_spi_cfg;
extern struct capi_i2c_config bsp_i2c_cfg;

/* GPIO port of the Click CS (port 8) and of INT/RST (port 7). */
extern struct capi_gpio_port_config bsp_click_cs_port_cfg;
extern struct capi_gpio_port_config bsp_click_ctl_port_cfg;

/* .port_handle is set by the application after capi_gpio_port_init(). */
extern struct capi_gpio_pin bsp_click_cs;
extern struct capi_gpio_pin bsp_click_int;
extern struct capi_gpio_pin bsp_click_rst;

/* capi_pinctrl (CONFIG_PINCTRL): the platform pin controller, and the
 * Click UART pads. */
extern struct capi_pinctrl_config bsp_pinctrl_cfg;
#define BSP_CLICK_UART_PINS	2
extern const struct capi_pinctrl_descriptor bsp_click_uart_pins[BSP_CLICK_UART_PINS];

#endif /* BOARD_PARAMETERS_H */
