/*
 * Copyright 2026 xorgxrdp contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef RDP_TRANSPORT_H
#define RDP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

/* Pixel data travels through shared memory. Bound queued protocol data and
 * descriptors when a local peer stops consuming it. */
#define RDP_TRANSPORT_MAX_QUEUED_BYTES (16 * 1024 * 1024)
#define RDP_TRANSPORT_MAX_QUEUED_ITEMS 4096
#define RDP_TRANSPORT_MAX_QUEUED_FDS 64
#define RDP_TRANSPORT_MAX_MESSAGE_BYTES (1024 * 1024)

struct rdpTransportItem;

/* Zero-initialize before first use; the socket remains owned by the caller. */
struct rdpTransport
{
    struct rdpTransportItem *head;
    struct rdpTransportItem *tail;
    size_t queued_bytes;
    unsigned int queued_items;
    unsigned int queued_fds;
    uint64_t queued_serial;
    uint64_t completed_serial;
    uint8_t header[4];
    size_t header_used;
    uint8_t *input;
    size_t input_size;
    size_t input_used;
    size_t input_expected;
};

/* Copy bytes, and duplicate fd if >= 0, into the ordered output queue.
 * Returns -1 on failure; no part of this item is queued in that case. */
int rdpTransportQueue(struct rdpTransport *transport, const void *data,
                      size_t bytes, int fd);

/* Nonblocking, bounded work. Returns -1 on fatal I/O, 0 otherwise.
 * Call again on write readiness while rdpTransportPending() is true. */
int rdpTransportFlush(struct rdpTransport *transport, int socket);
int rdpTransportPending(const struct rdpTransport *transport);

/* Read one length-prefixed message without blocking. Returns 1 for a complete
 * payload (excluding its four-byte length), 0 for incomplete, -1 on error/EOF.
 * The returned bytes remain valid until the next Receive/Destroy call. */
int rdpTransportReceive(struct rdpTransport *transport, int socket,
                        const uint8_t **data, size_t *bytes);

/* Free queued bytes and close all descriptors owned by the queue. */
void rdpTransportDestroy(struct rdpTransport *transport);

#endif
