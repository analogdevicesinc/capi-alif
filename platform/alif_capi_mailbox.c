/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Ops implementation for capi_mailbox.h against the DFP's real MHU driver
 * (drivers/include/mhu.h, drivers/source/mhu_driver.c) over the single
 * RTSS-HP<->RTSS-HE MHU pair (pair 0). Base addresses and IRQ numbers below
 * are read directly from the DFP's own soc.h for AE722F80F55D5 (confirmed
 * identical physical addresses viewed from both cores, just named from
 * each core's own perspective -- not guessed):
 *
 *   RTSS-HP: sends via MHU_M55HP_M55HE_0_TX_BASE (0x40090000, IRQ42 ack),
 *            receives via MHU_M55HE_M55HP_0_RX_BASE (0x40080000, IRQ41 rx)
 *   RTSS-HE: sends via MHU_M55HE_M55HP_0_TX_BASE (0x40080000, IRQ42 ack),
 *            receives via MHU_M55HP_M55HE_0_RX_BASE (0x40090000, IRQ41 rx)
 *
 * The MHU IP sends one 32-bit word per channel per message (see mhu.h:
 * MHU_send_message_t takes a plain uint32_t, not a buffer) -- this driver
 * uses channel 0 for up to 4 bytes of application payload (packed
 * little-endian into the word) and channel 1 purely as a doorbell (no
 * payload). capi_mailbox_transaction.msg_len > 4 on the data channel
 * returns -EMSGSIZE rather than silently truncating or splitting across
 * channels -- fine for hello_capi's short fixed-size greeting struct, not
 * a general byte-stream mailbox.
 */

#include "alif_capi_mailbox.h"
#include "capi_alloc.h"
#include CMSIS_device_header
#include "mhu.h"
#include <errno.h>
#include <string.h>

#define ALIF_MHU_CHAN_DATA     0U
#define ALIF_MHU_CHAN_DOORBELL 1U
#define ALIF_MHU_ID            0U

#if defined(RTSS_HP)
#define ALIF_MHU_TX_BASE MHU_M55HP_M55HE_0_TX_BASE
#define ALIF_MHU_RX_BASE MHU_M55HE_M55HP_0_RX_BASE
#define ALIF_MHU_RX_IRQn MHU_M55HE_M55HP_0_RX_IRQ_IRQn
#define ALIF_MHU_TX_IRQn MHU_M55HP_M55HE_0_TX_IRQ_IRQn
#define ALIF_MHU_RX_VECTOR MHU_M55HE_M55HP_0_RX_IRQHandler
#define ALIF_MHU_TX_VECTOR MHU_M55HP_M55HE_0_TX_IRQHandler
#elif defined(RTSS_HE)
#define ALIF_MHU_TX_BASE MHU_M55HE_M55HP_0_TX_BASE
#define ALIF_MHU_RX_BASE MHU_M55HP_M55HE_0_RX_BASE
#define ALIF_MHU_RX_IRQn MHU_M55HP_M55HE_0_RX_IRQ_IRQn
#define ALIF_MHU_TX_IRQn MHU_M55HE_M55HP_0_TX_IRQ_IRQn
#define ALIF_MHU_RX_VECTOR MHU_M55HP_M55HE_0_RX_IRQHandler
#define ALIF_MHU_TX_VECTOR MHU_M55HE_M55HP_0_TX_IRQHandler
#else
#error "alif_capi_mailbox.c needs RTSS_HP or RTSS_HE defined (set by capi_alif_config_sdk())"
#endif

struct alif_mailbox_priv {
	capi_mailbox_callback callback;
	void *callback_arg;
};

static uint32_t alif_mhu_sender_base = ALIF_MHU_TX_BASE;
static uint32_t alif_mhu_receiver_base = ALIF_MHU_RX_BASE;
static mhu_driver_out_t alif_mhu_out;
static struct alif_mailbox_priv *alif_mhu_active_priv;

static void alif_mhu_send_acked_cb(uint32_t mhu_id, uint32_t channel_number)
{
	struct capi_mailbox_transaction msg = {
		.src_id = ALIF_MAILBOX_PEER_RTSS,
		.dest_id = ALIF_MAILBOX_PEER_RTSS,
	};

	(void)mhu_id;
	(void)channel_number;

	if (alif_mhu_active_priv && alif_mhu_active_priv->callback)
		alif_mhu_active_priv->callback(CAPI_MAILBOX_EVENT_MSG_ACKNOWLEDGED,
						alif_mhu_active_priv->callback_arg, &msg);
}

static void alif_mhu_rx_cb(uint32_t mhu_id, uint32_t channel_number, uint32_t message_data)
{
	struct capi_mailbox_transaction msg = {
		.src_id = ALIF_MAILBOX_PEER_RTSS,
		.dest_id = ALIF_MAILBOX_PEER_RTSS,
		.buf = (uint8_t *)&message_data,
		.size = sizeof(message_data),
		.msg_len = sizeof(message_data),
	};

	(void)mhu_id;

	if (!alif_mhu_active_priv || !alif_mhu_active_priv->callback)
		return;

	if (channel_number == ALIF_MHU_CHAN_DOORBELL)
		alif_mhu_active_priv->callback(CAPI_MAILBOX_EVENT_MSG_DOORBELL,
						alif_mhu_active_priv->callback_arg, &msg);
	else
		alif_mhu_active_priv->callback(CAPI_MAILBOX_EVENT_MSG_RECIEVED,
						alif_mhu_active_priv->callback_arg, &msg);
}

void ALIF_MHU_RX_VECTOR(void)
{
	if (alif_mhu_out.receiver_irq_handler)
		alif_mhu_out.receiver_irq_handler(ALIF_MHU_ID);
}

void ALIF_MHU_TX_VECTOR(void)
{
	if (alif_mhu_out.sender_irq_handler)
		alif_mhu_out.sender_irq_handler(ALIF_MHU_ID);
}

static int alif_capi_mailbox_init(struct capi_mailbox_handle **handle,
				  const struct capi_mailbox_config *config)
{
	struct capi_mailbox_handle *mbox_handle;
	struct alif_mailbox_priv *priv;
	mhu_driver_in_t mhu_in = {
		.sender_base_address_list = &alif_mhu_sender_base,
		.receiver_base_address_list = &alif_mhu_receiver_base,
		.mhu_count = 1,
		.send_msg_acked_callback = alif_mhu_send_acked_cb,
		.rx_msg_callback = alif_mhu_rx_cb,
		.debug_print = NULL,
	};

	if (!handle || !config)
		return -EINVAL;

	if (*handle == NULL) {
		mbox_handle = (struct capi_mailbox_handle *)capi_calloc(1, sizeof(*mbox_handle));
		if (!mbox_handle)
			return -ENOMEM;

		priv = (struct alif_mailbox_priv *)capi_calloc(1, sizeof(*priv));
		if (!priv) {
			capi_free(mbox_handle);
			return -ENOMEM;
		}

		mbox_handle->priv = priv;
		mbox_handle->init_allocated = true;
	} else {
		mbox_handle = *handle;
		mbox_handle->init_allocated = false;

		if (!mbox_handle->priv)
			return -EINVAL;

		priv = mbox_handle->priv;
	}

	mbox_handle->ops = config->ops;
	alif_mhu_active_priv = priv;

	MHU_driver_initialize(&mhu_in, &alif_mhu_out);

	NVIC_ClearPendingIRQ(ALIF_MHU_RX_IRQn);
	NVIC_ClearPendingIRQ(ALIF_MHU_TX_IRQn);
	NVIC_EnableIRQ(ALIF_MHU_RX_IRQn);
	NVIC_EnableIRQ(ALIF_MHU_TX_IRQn);

	*handle = mbox_handle;

	return 0;
}

static int alif_capi_mailbox_deinit(struct capi_mailbox_handle *handle)
{
	if (!handle)
		return -EINVAL;

	NVIC_DisableIRQ(ALIF_MHU_RX_IRQn);
	NVIC_DisableIRQ(ALIF_MHU_TX_IRQn);
	alif_mhu_active_priv = NULL;

	if (handle->init_allocated) {
		capi_free(handle->priv);
		capi_free(handle);
	}

	return 0;
}

static int alif_mhu_send_word(uint32_t channel, uint32_t word)
{
	mhu_send_status_t st;

	if (!alif_mhu_out.send_message)
		return -ENODEV;

	st = alif_mhu_out.send_message(ALIF_MHU_ID, channel, word);

	return (st == MHU_SEND_OK || st == MHU_SEND_COMPLETED_OK) ? 0 : -EIO;
}

static int alif_capi_mailbox_transmit_sync(struct capi_mailbox_handle *handle,
					   struct capi_mailbox_transaction *msg)
{
	uint32_t word = 0;

	if (!handle || !msg || !msg->buf)
		return -EINVAL;
	if (msg->msg_len > sizeof(word))
		return -EMSGSIZE;

	memcpy(&word, msg->buf, msg->msg_len);

	return alif_mhu_send_word(ALIF_MHU_CHAN_DATA, word);
}

static int alif_capi_mailbox_transmit_async(struct capi_mailbox_handle *handle,
					    struct capi_mailbox_transaction *msg)
{
	/* MHU_send_message() already just posts the word and returns --
	 * genuinely no separate "async" path in this driver, sync and async
	 * are the same call. */
	return alif_capi_mailbox_transmit_sync(handle, msg);
}

static int alif_capi_mailbox_receive_sync(struct capi_mailbox_handle *handle,
					  struct capi_mailbox_transaction *msg)
{
	/* No blocking receive primitive in the MHU driver -- reception is
	 * inherently interrupt-driven (see alif_mhu_rx_cb() / register a
	 * callback via capi_mailbox_register_callback() instead). */
	(void)handle;
	(void)msg;
	return -ENOTSUP;
}

static int alif_capi_mailbox_receive_async(struct capi_mailbox_handle *handle,
					   struct capi_mailbox_transaction *msg)
{
	(void)handle;
	(void)msg;
	return -ENOTSUP;
}

static int alif_capi_mailbox_register_callback(struct capi_mailbox_handle *handle,
					       capi_mailbox_callback const callback,
					       void *const callback_arg)
{
	struct alif_mailbox_priv *priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;
	priv->callback = callback;
	priv->callback_arg = callback_arg;

	return 0;
}

static int alif_capi_mailbox_acknowledge(struct capi_mailbox_handle *handle,
					 capi_mailbox_id_t dest_id)
{
	/* Acking is handled internally by the MHU driver's protocol (see
	 * send_msg_acked_callback) -- nothing for the application to do. */
	(void)handle;
	(void)dest_id;
	return 0;
}

static int alif_capi_mailbox_ring_doorbell(struct capi_mailbox_handle *handle,
					   capi_mailbox_id_t dest_id)
{
	(void)handle;
	(void)dest_id;
	return alif_mhu_send_word(ALIF_MHU_CHAN_DOORBELL, 0);
}

static int alif_capi_mailbox_flush(struct capi_mailbox_handle *handle, capi_mailbox_id_t id)
{
	(void)handle;
	(void)id;
	return -ENOTSUP;
}

static void alif_capi_mailbox_isr(void *handle)
{
	/* Not used -- real vectors (ALIF_MHU_RX_VECTOR/ALIF_MHU_TX_VECTOR
	 * above) call straight into the MHU driver's own IRQ handlers,
	 * matching how the driver is designed to be wired (it owns the
	 * hardware ack/retry protocol internally). */
	(void)handle;
}

const struct capi_mailbox_ops alif_capi_mailbox_ops = {
	.init = alif_capi_mailbox_init,
	.deinit = alif_capi_mailbox_deinit,
	.transmit_sync = alif_capi_mailbox_transmit_sync,
	.receive_sync = alif_capi_mailbox_receive_sync,
	.register_callback = alif_capi_mailbox_register_callback,
	.transmit_async = alif_capi_mailbox_transmit_async,
	.receive_async = alif_capi_mailbox_receive_async,
	.acknowledge = alif_capi_mailbox_acknowledge,
	.ring_doorbell = alif_capi_mailbox_ring_doorbell,
	.flush = alif_capi_mailbox_flush,
	.isr = alif_capi_mailbox_isr,
};
