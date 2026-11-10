/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Ops implementation for capi_uart.h against Alif_CMSIS's ARM_DRIVER_USART
 * (the standard ARM CMSIS-Driver USART API -- Alif_CMSIS/Source/Driver_USART.c
 * implements one ARM_DRIVER_USART instance per enabled UART, gated by
 * RTE_Device.h's RTE_USARTn flags). Send()/Receive() are asynchronous by
 * the CMSIS-Driver spec (start a transfer, signal completion via the event
 * callback); capi_uart_transmit/receive wrap them with a busy-poll on
 * GetStatus().tx_busy/rx_busy to present capi's synchronous/blocking API --
 * fine for hello_capi/blinky's short console messages, not a substitute
 * for real IRQ-driven async transfer (transmit_async/receive_async are
 * stubbed, same scope decision as capi-maxim's uart driver).
 */

#include "alif_capi_uart.h"
#include "capi_alloc.h"
#include "Driver_USART.h"
#include <errno.h>

struct alif_uart_priv {
	ARM_DRIVER_USART *drv;
};

static struct capi_uart_handle *uart_stdio_handle;

/* DK-E7 muxes several UARTs (see Boards/DevKit-e7/pins.h): UART2 is
 * "UARTA" and UART4 is "UARTB"; the board's PRG USB second channel is
 * jumper-selectable (J26) between them, so both are wired up here. Add
 * more extern/case entries as other instances are needed.
 *
 * NOTE: an instance is only usable if its RTE_UART<n> AND
 * RTE_UART<n>_BLOCKING_MODE_ENABLE are both 1 in the board's RTE_Device.h
 * -- blocking mode is required because capi_uart_transmit() is synchronous
 * and this SoC's UART interrupt vectors are not populated (see the
 * platform README). */
extern ARM_DRIVER_USART Driver_USART2;
extern ARM_DRIVER_USART Driver_USART4;

static ARM_DRIVER_USART *alif_uart_driver_for(uint32_t identifier)
{
	switch (identifier) {
	case 2:
		return &Driver_USART2;
	case 4:
		return &Driver_USART4;
	default:
		return NULL;
	}
}

static void alif_uart_signal_event(uint32_t event)
{
	/* Only used to unblock the busy-poll loops below; no per-instance
	 * event queue needed since transmit()/receive() poll GetStatus()
	 * directly rather than waiting on this callback. */
	(void)event;
}

static int alif_capi_uart_init(struct capi_uart_handle **handle,
			       const struct capi_uart_config *config)
{
	struct capi_uart_handle *uart_handle;
	struct alif_uart_priv *priv;
	ARM_DRIVER_USART *drv;
	uint32_t baud = 115200;
	uint32_t mode;
	int32_t ret;

	if (!handle || !config)
		return -EINVAL;

	drv = alif_uart_driver_for(config->identifier);
	if (!drv)
		return -EINVAL;

	if (config->line_config)
		baud = config->line_config->baudrate;

	ret = drv->Initialize(alif_uart_signal_event);
	if (ret != ARM_DRIVER_OK)
		return -EIO;

	ret = drv->PowerControl(ARM_POWER_FULL);
	if (ret != ARM_DRIVER_OK)
		return -EIO;

	mode = ARM_USART_MODE_ASYNCHRONOUS | ARM_USART_DATA_BITS_8 |
	       ARM_USART_PARITY_NONE | ARM_USART_STOP_BITS_1 |
	       ARM_USART_FLOW_CONTROL_NONE;

	if (config->line_config) {
		switch (config->line_config->parity) {
		case CAPI_UART_PARITY_ODD:
			mode = (mode & ~ARM_USART_PARITY_Msk) | ARM_USART_PARITY_ODD;
			break;
		case CAPI_UART_PARITY_EVEN:
			mode = (mode & ~ARM_USART_PARITY_Msk) | ARM_USART_PARITY_EVEN;
			break;
		default:
			break;
		}
		if (config->line_config->stop_bits == CAPI_UART_STOP_2_BIT)
			mode = (mode & ~ARM_USART_STOP_BITS_Msk) | ARM_USART_STOP_BITS_2;
	}

	ret = drv->Control(mode, baud);
	if (ret != ARM_DRIVER_OK)
		return -EIO;

	ret = drv->Control(ARM_USART_CONTROL_TX, 1);
	if (ret != ARM_DRIVER_OK)
		return -EIO;
	ret = drv->Control(ARM_USART_CONTROL_RX, 1);
	if (ret != ARM_DRIVER_OK)
		return -EIO;

	if (*handle == NULL) {
		uart_handle = (struct capi_uart_handle *)capi_calloc(1, sizeof(*uart_handle));
		if (!uart_handle)
			return -ENOMEM;

		priv = (struct alif_uart_priv *)capi_calloc(1, sizeof(*priv));
		if (!priv) {
			capi_free(uart_handle);
			return -ENOMEM;
		}

		uart_handle->priv = priv;
		uart_handle->init_allocated = true;
	} else {
		uart_handle = *handle;
		uart_handle->init_allocated = false;

		if (!uart_handle->priv)
			return -EINVAL;

		priv = uart_handle->priv;
	}

	uart_handle->ops = config->ops;
	priv->drv = drv;

	*handle = uart_handle;

	return 0;
}

static int alif_capi_uart_deinit(struct capi_uart_handle *handle)
{
	struct alif_uart_priv *priv;

	if (!handle)
		return -EINVAL;

	priv = handle->priv;
	if (priv && priv->drv) {
		priv->drv->PowerControl(ARM_POWER_OFF);
		priv->drv->Uninitialize();
	}

	if (handle->init_allocated) {
		capi_free(priv);
		capi_free(handle);
	}

	return 0;
}

static int alif_capi_uart_get_line_config(struct capi_uart_handle *handle,
					  struct capi_uart_line_config *line_config)
{
	if (!handle || !handle->priv || !line_config)
		return -EINVAL;

	/* ARM_DRIVER_USART exposes no generic "read back current line
	 * config" call -- report what capi always assumes and leave the
	 * rest at their capi defaults, same approach as capi-maxim. */
	line_config->address_mode = CAPI_UART_ADDRESS_MODE_DISABLED;

	return 0;
}

static int alif_capi_uart_set_line_config(struct capi_uart_handle *handle,
					  struct capi_uart_line_config *line_config)
{
	struct alif_uart_priv *priv;
	uint32_t mode;
	int32_t ret;

	if (!handle || !handle->priv || !line_config)
		return -EINVAL;

	priv = handle->priv;

	mode = ARM_USART_MODE_ASYNCHRONOUS | ARM_USART_DATA_BITS_8 |
	       ARM_USART_STOP_BITS_1 | ARM_USART_FLOW_CONTROL_NONE;

	switch (line_config->parity) {
	case CAPI_UART_PARITY_ODD:
		mode = (mode & ~ARM_USART_PARITY_Msk) | ARM_USART_PARITY_ODD;
		break;
	case CAPI_UART_PARITY_EVEN:
		mode = (mode & ~ARM_USART_PARITY_Msk) | ARM_USART_PARITY_EVEN;
		break;
	default:
		mode = (mode & ~ARM_USART_PARITY_Msk) | ARM_USART_PARITY_NONE;
		break;
	}
	if (line_config->stop_bits == CAPI_UART_STOP_2_BIT)
		mode = (mode & ~ARM_USART_STOP_BITS_Msk) | ARM_USART_STOP_BITS_2;

	ret = priv->drv->Control(mode, line_config->baudrate);

	return (ret == ARM_DRIVER_OK) ? 0 : -EIO;
}

static int alif_capi_uart_enable_fifo(struct capi_uart_handle *handle, bool enable)
{
	(void)handle;
	(void)enable;
	/* FIFOs are managed internally by the CMSIS-Driver; nothing to toggle. */
	return 0;
}

static int alif_capi_uart_flush_tx_fifo(struct capi_uart_handle *handle)
{
	struct alif_uart_priv *priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;
	return (priv->drv->Control(ARM_USART_ABORT_SEND, 0) == ARM_DRIVER_OK) ? 0 : -EIO;
}

static int alif_capi_uart_flush_rx_fifo(struct capi_uart_handle *handle)
{
	struct alif_uart_priv *priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;
	return (priv->drv->Control(ARM_USART_ABORT_RECEIVE, 0) == ARM_DRIVER_OK) ? 0 : -EIO;
}

static int alif_capi_uart_get_rx_fifo_count(struct capi_uart_handle *handle, uint16_t *count)
{
	struct alif_uart_priv *priv;

	if (!handle || !handle->priv || !count)
		return -EINVAL;

	priv = handle->priv;
	*count = (uint16_t)priv->drv->GetRxCount();

	return 0;
}

static int alif_capi_uart_get_tx_fifo_count(struct capi_uart_handle *handle, uint16_t *count)
{
	struct alif_uart_priv *priv;

	if (!handle || !handle->priv || !count)
		return -EINVAL;

	priv = handle->priv;
	*count = (uint16_t)priv->drv->GetTxCount();

	return 0;
}

int alif_capi_uart_transmit(struct capi_uart_handle *handle, const uint8_t *buf, uint32_t len)
{
	struct alif_uart_priv *priv;
	int32_t ret;

	if (!handle || !handle->priv || !buf)
		return -EINVAL;

	priv = handle->priv;

	ret = priv->drv->Send(buf, len);
	if (ret != ARM_DRIVER_OK)
		return -EIO;

	while (priv->drv->GetStatus().tx_busy)
		;

	return 0;
}

static int alif_capi_uart_receive(struct capi_uart_handle *handle, uint8_t *buf, uint32_t len)
{
	struct alif_uart_priv *priv;
	int32_t ret;

	if (!handle || !handle->priv || !buf)
		return -EINVAL;

	priv = handle->priv;

	ret = priv->drv->Receive(buf, len);
	if (ret != ARM_DRIVER_OK)
		return -EIO;

	while (priv->drv->GetStatus().rx_busy)
		;

	return 0;
}

static uint32_t alif_capi_uart_read_byte(struct capi_uart_handle *handle, uint8_t *byte)
{
	if (!handle || !byte)
		return 0;

	return (alif_capi_uart_receive(handle, byte, 1) == 0) ? 1 : 0;
}

static uint32_t alif_capi_uart_write_byte(struct capi_uart_handle *handle, uint8_t byte)
{
	if (!handle)
		return 0;

	return (alif_capi_uart_transmit(handle, &byte, 1) == 0) ? 1 : 0;
}

static void alif_capi_uart_isr(void *handle)
{
	(void)handle;
	/* No IRQ-driven transfers wired up yet -- see file header comment. */
}

/* Deferred (see file header comment): each stubbed op keeps its exact
 * capi_uart.h signature so a future async/IRQ-driven port only needs to
 * replace these bodies, not the ops table. */
static int alif_capi_uart_register_callback(struct capi_uart_handle *handle,
					    capi_uart_callback const callback,
					    void *const callback_arg)
{
	(void)handle;
	(void)callback;
	(void)callback_arg;
	return -ENOTSUP;
}

static int alif_capi_uart_transmit_async(struct capi_uart_handle *handle, const uint8_t *buf,
					 uint32_t len)
{
	(void)handle;
	(void)buf;
	(void)len;
	return -ENOTSUP;
}

static int alif_capi_uart_receive_async(struct capi_uart_handle *handle, uint8_t *buf,
					uint32_t len)
{
	(void)handle;
	(void)buf;
	(void)len;
	return -ENOTSUP;
}

static int alif_capi_uart_get_interrupt_reason(struct capi_uart_handle *handle,
					       enum capi_uart_interrupt_reason *reason)
{
	(void)handle;
	if (reason)
		*reason = CAPI_UART_INTR_NONE;
	return -ENOTSUP;
}

static int alif_capi_uart_get_line_status(struct capi_uart_handle *handle, uint32_t *status_flags)
{
	(void)handle;
	if (status_flags)
		*status_flags = 0;
	return -ENOTSUP;
}

static int alif_capi_uart_transmit_9bit(struct capi_uart_handle *handle, uint16_t data,
					bool is_address)
{
	(void)handle;
	(void)data;
	(void)is_address;
	return -ENOTSUP;
}

static int alif_capi_uart_receive_9bit(struct capi_uart_handle *handle, uint16_t *data,
				       bool *is_address)
{
	(void)handle;
	if (data)
		*data = 0;
	if (is_address)
		*is_address = false;
	return -ENOTSUP;
}

static int alif_capi_uart_set_flow_control_state(struct capi_uart_handle *handle,
						 bool rts_state, bool cts_state)
{
	(void)handle;
	(void)rts_state;
	(void)cts_state;
	return -ENOTSUP;
}

static int alif_capi_uart_get_flow_control_state(struct capi_uart_handle *handle,
						 bool *rts_state, bool *cts_state)
{
	(void)handle;
	if (rts_state)
		*rts_state = false;
	if (cts_state)
		*cts_state = false;
	return -ENOTSUP;
}

static int alif_capi_uart_set_irq_tx(struct capi_uart_handle *handle, bool enable)
{
	(void)handle;
	(void)enable;
	return -ENOTSUP;
}

static int alif_capi_uart_irq_tx_ready(struct capi_uart_handle *handle, bool *ready)
{
	(void)handle;
	if (ready)
		*ready = false;
	return -ENOTSUP;
}

static int alif_capi_uart_irq_tx_complete(struct capi_uart_handle *handle, bool *complete)
{
	(void)handle;
	if (complete)
		*complete = false;
	return -ENOTSUP;
}

static int alif_capi_uart_set_irq_rx(struct capi_uart_handle *handle, bool enable)
{
	(void)handle;
	(void)enable;
	return -ENOTSUP;
}

static int alif_capi_uart_irq_rx_ready(struct capi_uart_handle *handle, bool *ready)
{
	(void)handle;
	if (ready)
		*ready = false;
	return -ENOTSUP;
}

static int alif_capi_uart_set_irq_err(struct capi_uart_handle *handle, bool enable)
{
	(void)handle;
	(void)enable;
	return -ENOTSUP;
}

static int alif_capi_uart_is_irq_pending(struct capi_uart_handle *handle, bool *pending)
{
	(void)handle;
	if (pending)
		*pending = false;
	return -ENOTSUP;
}

const struct capi_uart_ops alif_capi_uart_ops = {
	.init = alif_capi_uart_init,
	.deinit = alif_capi_uart_deinit,
	.get_line_config = alif_capi_uart_get_line_config,
	.set_line_config = alif_capi_uart_set_line_config,
	.enable_fifo = alif_capi_uart_enable_fifo,
	.flush_tx_fifo = alif_capi_uart_flush_tx_fifo,
	.flush_rx_fifo = alif_capi_uart_flush_rx_fifo,
	.get_rx_fifo_count = alif_capi_uart_get_rx_fifo_count,
	.get_tx_fifo_count = alif_capi_uart_get_tx_fifo_count,
	.transmit = alif_capi_uart_transmit,
	.receive = alif_capi_uart_receive,
	.register_callback = alif_capi_uart_register_callback,
	.transmit_async = alif_capi_uart_transmit_async,
	.receive_async = alif_capi_uart_receive_async,
	.get_interrupt_reason = alif_capi_uart_get_interrupt_reason,
	.get_line_status = alif_capi_uart_get_line_status,
	.transmit_9bit = alif_capi_uart_transmit_9bit,
	.receive_9bit = alif_capi_uart_receive_9bit,
	.set_flow_control_state = alif_capi_uart_set_flow_control_state,
	.get_flow_control_state = alif_capi_uart_get_flow_control_state,
	.isr = alif_capi_uart_isr,
	.read_byte = alif_capi_uart_read_byte,
	.write_byte = alif_capi_uart_write_byte,
	.set_irq_tx = alif_capi_uart_set_irq_tx,
	.irq_tx_ready = alif_capi_uart_irq_tx_ready,
	.irq_tx_complete = alif_capi_uart_irq_tx_complete,
	.set_irq_rx = alif_capi_uart_set_irq_rx,
	.irq_rx_ready = alif_capi_uart_irq_rx_ready,
	.set_irq_err = alif_capi_uart_set_irq_err,
	.is_irq_pending = alif_capi_uart_is_irq_pending,
};

int alif_uart_stdio_enable(struct capi_uart_handle *handle)
{
	uart_stdio_handle = handle;
	return 0;
}

/* newlib syscall retarget -- printf()/putchar() route through _write(). */
int _write(int fd, char *buf, int len)
{
	(void)fd;

	if (!uart_stdio_handle)
		return -1;

	if (alif_capi_uart_transmit(uart_stdio_handle, (uint8_t *)buf, (uint32_t)len) != 0)
		return -1;

	return len;
}
