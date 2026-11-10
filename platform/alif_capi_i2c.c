/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Ops implementation for capi_i2c.h, controller mode only: I2C0..3 through
 * Alif_CMSIS's ARM_DRIVER_I2C (Driver_I2C.c), and the I3C controller
 * speaking legacy I2C through ARM_DRIVER_I3C (Driver_I3C.c) directly.
 *
 * Transfers are interrupt-driven (soc_vectors.h maps I2C0..3 and I3C);
 * each call waits for the driver's completion event, so the caller sees a
 * blocking transfer. A NACKed address is reported as -ENXIO, so a probe
 * can tell "no device there" from a bus fault.
 *
 * A register read is the sub-address written without a STOP, then a
 * repeated START and the read.
 *
 * The I3C controller addresses targets through a device table, so each
 * transfer attaches its address, runs, and detaches it -- after completion.
 * Alif's own I2C adapter for it (Driver_I3C_I2C.c) is not used: it detaches
 * as soon as the transfer call returns, mid-transfer unless that call
 * blocks, and its blocking mode waits without a timeout (seen on DK-E7: a
 * hard hang on the first write). It also has no repeated START, so there
 * a register read is two transfers with a STOP between, which sensors that
 * latch the register address, like DK-E7's IMUs, read the same. A failed
 * I3C transfer does not say why, so a NACK is not told from a bus fault
 * there; it is returned as -ENXIO, which is what it is in practice.
 */

#include "alif_capi_i2c.h"
#include "capi_alloc.h"
#include "capi_time.h"
#include "Driver_I2C.h"
#include "Driver_I3C.h"
#include "i2c.h"
#include "soc.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define ALIF_I2C_INSTANCES	5
#define ALIF_I2C_TIMEOUT_MS	100
/* Sub-address and data go out as one write, so they share a buffer. */
#define ALIF_I2C_WRITE_MAX	64

extern ARM_DRIVER_I2C Driver_I2C0;
extern ARM_DRIVER_I2C Driver_I2C1;
extern ARM_DRIVER_I2C Driver_I2C2;
extern ARM_DRIVER_I2C Driver_I2C3;
extern ARM_DRIVER_I3C Driver_I3C;

struct alif_i2c_priv {
	ARM_DRIVER_I2C *drv;
	uint32_t instance;
	enum capi_i2c_speed speed;
	bool speed_set;
};

/* The CMSIS callback carries no context, so one per instance. */
static volatile uint32_t alif_i2c_events[ALIF_I2C_INSTANCES];
static bool alif_i2c_in_use[ALIF_I2C_INSTANCES];

#define ALIF_I2C_EVENT_FN(n) \
	static void alif_i2c_event##n(uint32_t event) { alif_i2c_events[n] |= event; }
ALIF_I2C_EVENT_FN(0)
ALIF_I2C_EVENT_FN(1)
ALIF_I2C_EVENT_FN(2)
ALIF_I2C_EVENT_FN(3)
ALIF_I2C_EVENT_FN(4)

static const ARM_I2C_SignalEvent_t alif_i2c_event_fns[ALIF_I2C_INSTANCES] = {
	alif_i2c_event0, alif_i2c_event1, alif_i2c_event2, alif_i2c_event3, alif_i2c_event4,
};

static ARM_DRIVER_I2C *alif_i2c_driver_for(uint32_t instance)
{
	switch (instance) {
	case 0:
		return &Driver_I2C0;
	case 1:
		return &Driver_I2C1;
	case 2:
		return &Driver_I2C2;
	case 3:
		return &Driver_I2C3;
	default:
		return NULL;
	}
}

static volatile uint32_t alif_i3c_events;

static void alif_i3c_event(uint32_t event)
{
	alif_i3c_events |= event;
}

/* Bring the I3C controller up as a controller on a mixed I3C/I2C bus at
 * the given I2C speed, as Alif's own I2C-device demos do. */
static int alif_i3c_set_speed(enum capi_i2c_speed speed)
{
	uint32_t mode;

	switch (speed) {
	case CAPI_I2C_SPEED_STANDARD:
		mode = I3C_BUS_MODE_MIXED_SLOW_I2C_SS_SPEED_100_KBPS;
		break;
	case CAPI_I2C_SPEED_FAST:
		mode = I3C_BUS_MODE_MIXED_FAST_I2C_FM_SPEED_400_KBPS;
		break;
	case CAPI_I2C_SPEED_FAST_PLUS:
		mode = I3C_BUS_MODE_MIXED_FAST_I2C_FMP_SPEED_1_MBPS;
		break;
	default:
		return -ENOTSUP;
	}
	if (Driver_I3C.Control(I3C_MASTER_INIT, 0) != ARM_DRIVER_OK ||
	    Driver_I3C.Control(I3C_MASTER_SET_BUS_MODE, mode) != ARM_DRIVER_OK ||
	    Driver_I3C.Control(I3C_MASTER_SETUP_HOT_JOIN_ACCEPTANCE, 0) != ARM_DRIVER_OK ||
	    Driver_I3C.Control(I3C_MASTER_SETUP_MR_ACCEPTANCE, 0) != ARM_DRIVER_OK ||
	    Driver_I3C.Control(I3C_MASTER_SETUP_SIR_ACCEPTANCE, 0) != ARM_DRIVER_OK)
		return -EIO;
	return 0;
}

/* One I2C transfer on the I3C controller: attach, run, wait, detach. */
static int alif_i3c_transfer(uint16_t addr, bool write, uint8_t *buf, uint32_t len)
{
	uint64_t start = 0, now = 0;
	int32_t r;
	int ret = 0;

	if (Driver_I3C.AttachSlvDev(ARM_I3C_DEVICE_TYPE_I2C, (uint8_t)addr) != ARM_DRIVER_OK)
		return -EIO;
	alif_i3c_events = 0;
	r = write ? Driver_I3C.MasterTransmit((uint8_t)addr, buf, (uint16_t)len) :
		    Driver_I3C.MasterReceive((uint8_t)addr, buf, (uint16_t)len);
	if (r != ARM_DRIVER_OK) {
		ret = -EIO;
		goto detach;
	}
	capi_uptime(&start);
	while (!(alif_i3c_events & (ARM_I3C_EVENT_TRANSFER_DONE | ARM_I3C_EVENT_TRANSFER_ERROR))) {
		capi_uptime(&now);
		if (now - start > ALIF_I2C_TIMEOUT_MS * 1000U) {
			Driver_I3C.Control(I3C_MASTER_ABORT_MESSAGE_TRANSFER, 0);
			ret = -ETIMEDOUT;
			goto detach;
		}
	}
	if (alif_i3c_events & ARM_I3C_EVENT_TRANSFER_ERROR)
		ret = -ENXIO;
detach:
	Driver_I3C.Detachdev((uint8_t)addr);
	return ret;
}

static int alif_i2c_set_speed(struct alif_i2c_priv *priv, enum capi_i2c_speed speed)
{
	uint32_t arg;
	int ret;

	if (priv->instance == ALIF_CAPI_I2C_ON_I3C) {
		ret = alif_i3c_set_speed(speed);
		if (!ret) {
			priv->speed = speed;
			priv->speed_set = true;
		}
		return ret;
	}

	switch (speed) {
	case CAPI_I2C_SPEED_STANDARD:
		arg = ARM_I2C_BUS_SPEED_STANDARD;
		break;
	case CAPI_I2C_SPEED_FAST:
		arg = ARM_I2C_BUS_SPEED_FAST;
		break;
	case CAPI_I2C_SPEED_FAST_PLUS:
		arg = ARM_I2C_BUS_SPEED_FAST_PLUS;
		break;
	case CAPI_I2C_SPEED_HIGH:
		arg = ARM_I2C_BUS_SPEED_HIGH;
		break;
	default:
		return -ENOTSUP;
	}
	if (priv->drv->Control(ARM_I2C_BUS_SPEED, arg) != ARM_DRIVER_OK)
		return -EIO;
	priv->speed = speed;
	priv->speed_set = true;
	return 0;
}

static int alif_capi_i2c_init(struct capi_i2c_controller_handle **handle,
			      const struct capi_i2c_config *config)
{
	struct capi_i2c_controller_handle *h;
	struct alif_i2c_priv *priv;
	const struct alif_capi_i2c_extra *extra;
	ARM_DRIVER_I2C *drv;
	uint32_t n;
	int ret;

	if (!handle || !config)
		return -EINVAL;
	if (!config->initiator)
		return -ENOTSUP;	/* target mode is not implemented */

	n = (uint32_t)config->identifier;
	drv = alif_i2c_driver_for(n);
	if (!drv && n != ALIF_CAPI_I2C_ON_I3C)
		return -ENODEV;
	if (alif_i2c_in_use[n])
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
	priv->instance = n;

	if (n == ALIF_CAPI_I2C_ON_I3C) {
		if (Driver_I3C.Initialize(alif_i3c_event) != ARM_DRIVER_OK ||
		    Driver_I3C.PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) {
			Driver_I3C.Uninitialize();
			capi_free(h);
			capi_free(priv);
			return -EIO;
		}
	} else if (drv->Initialize(alif_i2c_event_fns[n]) != ARM_DRIVER_OK ||
	    drv->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) {
		drv->Uninitialize();
		capi_free(h);
		capi_free(priv);
		return -EIO;
	}

	h->ops = &alif_capi_i2c_ops;
	h->init_allocated = true;
	h->priv = priv;
	alif_i2c_in_use[n] = true;
	*handle = h;
	return 0;
}

static int alif_capi_i2c_deinit(struct capi_i2c_controller_handle *handle)
{
	struct alif_i2c_priv *priv;

	if (!handle || !handle->priv)
		return -EINVAL;
	priv = handle->priv;
	if (priv->instance == ALIF_CAPI_I2C_ON_I3C) {
		Driver_I3C.PowerControl(ARM_POWER_OFF);
		Driver_I3C.Uninitialize();
	} else {
		priv->drv->PowerControl(ARM_POWER_OFF);
		priv->drv->Uninitialize();
	}
	alif_i2c_in_use[priv->instance] = false;
	capi_free(priv);
	if (handle->init_allocated)
		capi_free(handle);
	return 0;
}

static struct alif_i2c_priv *alif_i2c_prepare(struct capi_i2c_device *device)
{
	struct alif_i2c_priv *priv;

	if (!device || !device->controller || !device->controller->priv)
		return NULL;
	priv = device->controller->priv;
	if ((!priv->speed_set || priv->speed != device->speed) &&
	    alif_i2c_set_speed(priv, device->speed))
		return NULL;
	return priv;
}

static I2C_Type *alif_i2c_regs_for(uint32_t instance)
{
	switch (instance) {
	case 0:
		return I2C0;
	case 1:
		return I2C1;
	case 2:
		return I2C2;
	case 3:
		return I2C3;
	default:
		return NULL;
	}
}

/* Wait for the transfer started just before to finish, and say how.
 * @stop: the transfer ended with a STOP, so the controller must also go
 * idle before the next one; without one (a register address before a
 * repeated START) it keeps the bus on purpose and stays active -- unless
 * it failed, since an abort sends a STOP regardless. */
static int alif_i2c_wait(struct alif_i2c_priv *priv, bool stop)
{
	uint64_t start = 0, now = 0;
	bool timed_out = false;
	uint32_t ev, failed;

	capi_uptime(&start);
	while (!(alif_i2c_events[priv->instance] & ARM_I2C_EVENT_TRANSFER_DONE) &&
	       !(alif_i2c_events[priv->instance] & (ARM_I2C_EVENT_ADDRESS_NACK |
						    ARM_I2C_EVENT_ARBITRATION_LOST |
						    ARM_I2C_EVENT_BUS_ERROR))) {
		capi_uptime(&now);
		if (now - start > ALIF_I2C_TIMEOUT_MS * 1000U) {
			priv->drv->Control(ARM_I2C_ABORT_TRANSFER, 0);
			return -ETIMEDOUT;
		}
	}
	/* The driver may still be finishing its STOP after the event. */
	while (priv->drv->GetStatus().busy) {
		capi_uptime(&now);
		if (now - start > ALIF_I2C_TIMEOUT_MS * 1000U)
			return -ETIMEDOUT;
	}

	/*
	 * And the controller itself may still be on the bus: after an address
	 * NACK the driver reports done while the abort and its STOP are still
	 * going out. Arming the next transfer then can wedge the driver in
	 * its interrupt handler, where no timeout here can help -- seen on
	 * DK-E8 as a back-to-back scan of I2C0 that never returned, while the
	 * same scan with a printf between probes completed.
	 */
	ev = alif_i2c_events[priv->instance];
	failed = ev & (ARM_I2C_EVENT_ADDRESS_NACK |
		       ARM_I2C_EVENT_ARBITRATION_LOST | ARM_I2C_EVENT_BUS_ERROR);
	if (stop || failed) {
		I2C_Type *regs = alif_i2c_regs_for(priv->instance);

		while (regs && (regs->I2C_STATUS & (I2C_IC_STATUS_ACTIVITY |
						    I2C_IC_STATUS_MASTER_ACT))) {
			capi_uptime(&now);
			if (now - start > ALIF_I2C_TIMEOUT_MS * 1000U) {
				timed_out = true;
				break;
			}
		}
	}

	/* A failure already reported wins over a slow return to idle. */
	if (ev & ARM_I2C_EVENT_ADDRESS_NACK)
		return -ENXIO;
	if (ev & (ARM_I2C_EVENT_ARBITRATION_LOST | ARM_I2C_EVENT_BUS_ERROR |
		  ARM_I2C_EVENT_TRANSFER_INCOMPLETE))
		return -EIO;
	return timed_out ? -ETIMEDOUT : 0;
}

static int alif_i2c_write(struct alif_i2c_priv *priv, uint16_t addr, const uint8_t *buf,
			  uint32_t len, bool pending)
{
	if (priv->instance == ALIF_CAPI_I2C_ON_I3C)
		return alif_i3c_transfer(addr, true, (uint8_t *)buf, len);
	alif_i2c_events[priv->instance] = 0;
	if (priv->drv->MasterTransmit(addr, buf, len, pending) != ARM_DRIVER_OK)
		return -EIO;
	return alif_i2c_wait(priv, !pending);
}

static int alif_i2c_read(struct alif_i2c_priv *priv, uint16_t addr, uint8_t *buf,
			 uint32_t len, bool pending)
{
	if (priv->instance == ALIF_CAPI_I2C_ON_I3C)
		return alif_i3c_transfer(addr, false, buf, len);
	alif_i2c_events[priv->instance] = 0;
	if (priv->drv->MasterReceive(addr, buf, len, pending) != ARM_DRIVER_OK)
		return -EIO;
	return alif_i2c_wait(priv, !pending);
}

static uint16_t alif_i2c_addr(const struct capi_i2c_device *device,
			      const struct capi_i2c_transfer *transfer)
{
	uint16_t a = transfer->target_addr ? transfer->target_addr : device->address;

	return device->b10addr ? (uint16_t)(a | ARM_I2C_ADDRESS_10BIT) : a;
}

static int alif_capi_i2c_transmit(struct capi_i2c_device *device,
				  struct capi_i2c_transfer *transfer)
{
	struct alif_i2c_priv *priv = alif_i2c_prepare(device);
	uint8_t buf[ALIF_I2C_WRITE_MAX];
	uint32_t n;

	if (!priv || !transfer || (transfer->len && !transfer->buf))
		return -EINVAL;

	n = transfer->sub_address_len + transfer->len;
	if (n > sizeof(buf))
		return -EMSGSIZE;
	if (!n)
		return -EINVAL;
	if (transfer->sub_address_len)
		memcpy(buf, transfer->sub_address, transfer->sub_address_len);
	if (transfer->len)
		memcpy(buf + transfer->sub_address_len, transfer->buf, transfer->len);

	return alif_i2c_write(priv, alif_i2c_addr(device, transfer), buf, n,
			      transfer->no_stop);
}

static int alif_capi_i2c_receive(struct capi_i2c_device *device,
				 struct capi_i2c_transfer *transfer)
{
	struct alif_i2c_priv *priv = alif_i2c_prepare(device);
	uint16_t addr;
	int ret;

	if (!priv || !transfer || !transfer->buf || !transfer->len)
		return -EINVAL;

	addr = alif_i2c_addr(device, transfer);
	if (transfer->sub_address_len) {
		ret = alif_i2c_write(priv, addr, transfer->sub_address,
				     transfer->sub_address_len, true);
		if (ret)
			return ret;
	}
	return alif_i2c_read(priv, addr, transfer->buf, transfer->len, transfer->no_stop);
}

static int alif_capi_i2c_configure_bus_speed(struct capi_i2c_controller_handle *handle,
					     enum capi_i2c_speed speed, uint8_t duty_cycle)
{
	(void)duty_cycle;
	if (!handle || !handle->priv)
		return -EINVAL;
	return alif_i2c_set_speed(handle->priv, speed);
}

static int alif_capi_i2c_recover_bus(struct capi_i2c_controller_handle *handle)
{
	struct alif_i2c_priv *priv;

	if (!handle || !handle->priv)
		return -EINVAL;
	priv = handle->priv;
	if (priv->instance == ALIF_CAPI_I2C_ON_I3C)
		return -ENOTSUP;
	/* Nine clocks and a STOP, to release a target holding SDA low. */
	return priv->drv->Control(ARM_I2C_BUS_CLEAR, 0) == ARM_DRIVER_OK ? 0 : -EIO;
}

const struct capi_i2c_ops alif_capi_i2c_ops = {
	.init = alif_capi_i2c_init,
	.deinit = alif_capi_i2c_deinit,
	.transmit = alif_capi_i2c_transmit,
	.receive = alif_capi_i2c_receive,
	.register_callback = NULL,
	.configure_bus_speed = alif_capi_i2c_configure_bus_speed,
	.transmit_async = NULL,
	.receive_async = NULL,
	.recover_bus = alif_capi_i2c_recover_bus,
	.register_target = NULL,
	.unregister_target = NULL,
	.isr = NULL,
};
