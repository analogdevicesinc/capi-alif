/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Ops implementation for capi_spi.h against Alif_CMSIS's ARM_DRIVER_SPI
 * (Alif_CMSIS/Source/Driver_SPI.c), controller mode only.
 *
 * SPI0..3 transfer by interrupt -- unlike the UART, these vectors ARE
 * populated (soc_vectors.h maps IRQ137..140 to SPIn_IRQHandler) -- and
 * capi_spi_transceive() waits for the driver to clear `busy`, so the
 * caller sees a blocking transfer.
 *
 * Chip-select is the device's GPIO (capi_spi_device.cs_gpio), driven by
 * set_cs() in the usual order: assert, transceive, de-assert. The
 * controller's own SS pins are left unused.
 */

#include "alif_capi_spi.h"
#include "capi_alloc.h"
#include "capi_gpio.h"
#include "capi_time.h"
#include "Driver_SPI.h"
#include "soc.h"
#include <errno.h>
#include <stdbool.h>

#define ALIF_SPI_INSTANCES	4
#define ALIF_SPI_DEFAULT_HZ	1000000U
/* CTRLR0.SRL, "shift register loop": MOSI fed back to MISO inside the
 * controller. Bit 13, between SLV_OE (12) and SSTE (14) in drivers/
 * include/spi.h, which does not name it. */
#define ALIF_SPI_CTRLR0_SRL	(1UL << 13)

extern ARM_DRIVER_SPI Driver_SPI0;
extern ARM_DRIVER_SPI Driver_SPI1;
extern ARM_DRIVER_SPI Driver_SPI2;
extern ARM_DRIVER_SPI Driver_SPI3;

struct alif_spi_priv {
	ARM_DRIVER_SPI *drv;
	SPI_Type *regs;
	uint32_t instance;
	bool loopback;
	/* The format last programmed, so a run of transfers to one device
	 * does not reprogram the controller each time. */
	bool configured;
	uint32_t speed_hz;
	enum capi_spi_mode mode;
	bool lsb_first;
};

static struct alif_spi_priv *alif_spi_active[ALIF_SPI_INSTANCES];

/* Completion is read from GetStatus(); the driver still requires a
 * callback, since its interrupt handler calls it unconditionally. */
static void alif_spi_event(uint32_t event)
{
	(void)event;
}

static ARM_DRIVER_SPI *alif_spi_driver_for(uint32_t instance, SPI_Type **regs)
{
	switch (instance) {
	case 0:
		*regs = SPI0;
		return &Driver_SPI0;
	case 1:
		*regs = SPI1;
		return &Driver_SPI1;
	case 2:
		*regs = SPI2;
		return &Driver_SPI2;
	case 3:
		*regs = SPI3;
		return &Driver_SPI3;
	default:
		return NULL;
	}
}

static uint32_t alif_spi_frame_format(enum capi_spi_mode mode)
{
	switch (mode) {
	case CAPI_SPI_MODE_1:
		return ARM_SPI_CPOL0_CPHA1;
	case CAPI_SPI_MODE_2:
		return ARM_SPI_CPOL1_CPHA0;
	case CAPI_SPI_MODE_3:
		return ARM_SPI_CPOL1_CPHA1;
	case CAPI_SPI_MODE_0:
	default:
		return ARM_SPI_CPOL0_CPHA0;
	}
}

static void alif_spi_set_loopback(struct alif_spi_priv *priv)
{
	/* CTRLR0 is writable only with the controller disabled; the
	 * driver's own register updates keep this bit. */
	uint32_t en = priv->regs->SPI_ENR;

	priv->regs->SPI_ENR = 0;
	if (priv->loopback)
		priv->regs->SPI_CTRLR0 |= ALIF_SPI_CTRLR0_SRL;
	else
		priv->regs->SPI_CTRLR0 &= ~ALIF_SPI_CTRLR0_SRL;
	priv->regs->SPI_ENR = en;
}

static int alif_spi_select_format(struct alif_spi_priv *priv,
				  const struct capi_spi_device *device)
{
	uint32_t speed = device->max_speed_hz ? device->max_speed_hz : ALIF_SPI_DEFAULT_HZ;
	uint32_t control;

	if (priv->configured && speed == priv->speed_hz && device->mode == priv->mode &&
	    device->lsb_first == priv->lsb_first)
		return 0;

	control = ARM_SPI_MODE_MASTER | alif_spi_frame_format(device->mode) |
		  (device->lsb_first ? ARM_SPI_LSB_MSB : ARM_SPI_MSB_LSB) |
		  ARM_SPI_SS_MASTER_UNUSED | ARM_SPI_DATA_BITS(8);
	if (priv->drv->Control(control, speed) != ARM_DRIVER_OK)
		return -EIO;
	alif_spi_set_loopback(priv);

	priv->configured = true;
	priv->speed_hz = speed;
	priv->mode = device->mode;
	priv->lsb_first = device->lsb_first;
	return 0;
}

static int alif_capi_spi_init(struct capi_spi_controller_handle **handle,
			      const struct capi_spi_config *config)
{
	struct capi_spi_controller_handle *h;
	struct alif_spi_priv *priv;
	const struct alif_capi_spi_extra *extra;
	SPI_Type *regs = NULL;
	ARM_DRIVER_SPI *drv;
	int ret;

	if (!handle || !config)
		return -EINVAL;

	drv = alif_spi_driver_for((uint32_t)config->identifier, &regs);
	if (!drv)
		return -ENODEV;
	if (alif_spi_active[config->identifier])
		return -EBUSY;

	extra = config->extra;
	if (extra) {
		ret = alif_capi_pinmux_apply(extra->pins, extra->n_pins);
		if (ret)
			return ret;
	}

	h = capi_calloc(1, sizeof(*h));
	priv = capi_calloc(1, sizeof(*priv));
	if (!h || !priv) {
		capi_free(h);
		capi_free(priv);
		return -ENOMEM;
	}
	priv->drv = drv;
	priv->regs = regs;
	priv->instance = (uint32_t)config->identifier;
	priv->loopback = config->loopback;

	if (drv->Initialize(alif_spi_event) != ARM_DRIVER_OK ||
	    drv->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) {
		drv->Uninitialize();
		capi_free(h);
		capi_free(priv);
		return -EIO;
	}

	h->ops = &alif_capi_spi_ops;
	h->init_allocated = true;
	h->priv = priv;
	alif_spi_active[priv->instance] = priv;
	*handle = h;
	return 0;
}

static int alif_capi_spi_deinit(struct capi_spi_controller_handle *handle)
{
	struct alif_spi_priv *priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;
	priv->drv->PowerControl(ARM_POWER_OFF);
	priv->drv->Uninitialize();
	alif_spi_active[priv->instance] = NULL;
	capi_free(priv);
	if (handle->init_allocated)
		capi_free(handle);
	return 0;
}

static int alif_spi_wait(struct alif_spi_priv *priv, int timeout_ms)
{
	uint64_t start = 0, now = 0;

	capi_uptime(&start);
	for (;;) {
		ARM_SPI_STATUS st = priv->drv->GetStatus();

		if (!st.busy)
			return st.data_lost ? -EIO : 0;
		if (timeout_ms > 0) {
			capi_uptime(&now);
			if (now - start > (uint64_t)timeout_ms * 1000U) {
				priv->drv->Control(ARM_SPI_ABORT_TRANSFER, 0);
				return -ETIMEDOUT;
			}
		}
	}
}

static int alif_capi_spi_transceive(struct capi_spi_device *device,
				    struct capi_spi_transfer *transfer)
{
	struct alif_spi_priv *priv;
	uint32_t n;
	int32_t ret;

	if (!device || !device->controller || !device->controller->priv || !transfer)
		return -EINVAL;
	priv = device->controller->priv;

	n = transfer->tx_size > transfer->rx_size ? transfer->tx_size : transfer->rx_size;
	if (!n)
		return 0;
	if (alif_spi_select_format(priv, device))
		return -EIO;

	if (transfer->tx_buf && transfer->rx_buf) {
		/* tx_buf == rx_buf is fine: a byte is received only after it
		 * has been sent. */
		ret = priv->drv->Transfer(transfer->tx_buf, transfer->rx_buf, n);
	} else if (transfer->tx_buf) {
		ret = priv->drv->Send(transfer->tx_buf, transfer->tx_size);
	} else if (transfer->rx_buf) {
		/* Zeros out while reading, per capi_spi_transfer. */
		priv->drv->Control(ARM_SPI_SET_DEFAULT_TX_VALUE, 0);
		ret = priv->drv->Receive(transfer->rx_buf, transfer->rx_size);
	} else {
		return -EINVAL;
	}
	if (ret != ARM_DRIVER_OK)
		return ret == ARM_DRIVER_ERROR_BUSY ? -EBUSY : -EIO;

	return alif_spi_wait(priv, transfer->timeout);
}

static int alif_capi_spi_set_cs(struct capi_spi_device *device,
				enum capi_spi_cs_control cs)
{
	if (!device)
		return -EINVAL;
	if (!device->cs_gpio)
		return 0;

	switch (cs) {
	case CAPI_SPI_CS_MANUAL_ASSERT:
		/*
		 * Program the device's format before selecting it. Control()
		 * and the loopback update disable and re-enable the controller,
		 * which can move SCLK; with CS already low the device can count
		 * that as a clock. On DK-E8 this made ADXL367 reads vary from
		 * one transfer to the next; with it they are repeatable.
		 */
		if (device->controller && device->controller->priv &&
		    alif_spi_select_format(device->controller->priv, device))
			return -EIO;
		return capi_gpio_pin_set_raw_value(device->cs_gpio, CAPI_GPIO_LOW);
	case CAPI_SPI_CS_MANUAL_DEASSERT:
		return capi_gpio_pin_set_raw_value(device->cs_gpio, CAPI_GPIO_HIGH);
	case CAPI_SPI_CS_AUTO:
	default:
		return 0;
	}
}

const struct capi_spi_ops alif_capi_spi_ops = {
	.init = alif_capi_spi_init,
	.deinit = alif_capi_spi_deinit,
	.transceive = alif_capi_spi_transceive,
	.transceive_async = NULL,
	.read_command = NULL,
	.read_command_async = NULL,
	.abort_async = NULL,
	.register_callback = NULL,
	.set_cs = alif_capi_spi_set_cs,
	.isr = NULL,
};
