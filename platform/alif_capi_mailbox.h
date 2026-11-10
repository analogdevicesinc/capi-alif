/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ALIF_CAPI_MAILBOX_H
#define ALIF_CAPI_MAILBOX_H

#include "capi_mailbox.h"

/* The only peer reachable from either RTSS-HP or RTSS-HE via this driver --
 * pass as capi_mailbox_transaction.dest_id / src_id. There is exactly one
 * other RTSS core to talk to, so one fixed id covers both directions. */
#define ALIF_MAILBOX_PEER_RTSS 0U

extern const struct capi_mailbox_ops alif_capi_mailbox_ops;

#endif /* ALIF_CAPI_MAILBOX_H */
