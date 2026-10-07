/*
 * Copyright 2026 xorgxrdp contributors
 * SPDX-License-Identifier: MIT
 */

#include "rdpTransport.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Yield to the Xorg event loop even if a peer continuously consumes data. */
#define TRANSPORT_IO_BYTES (256 * 1024)
#define TRANSPORT_IO_CALLS 64

struct rdpTransportItem
{
    struct rdpTransportItem *next;
    size_t bytes;
    size_t offset;
    int fd;
    uint64_t serial;
    uint8_t data[];
};

int
rdpTransportQueue(struct rdpTransport *transport, const void *data,
                  size_t bytes, int fd)
{
    struct rdpTransportItem *item;

    if (bytes == 0 || data == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (bytes > RDP_TRANSPORT_MAX_QUEUED_BYTES - transport->queued_bytes ||
            transport->queued_items >= RDP_TRANSPORT_MAX_QUEUED_ITEMS ||
            (fd >= 0 && transport->queued_fds >= RDP_TRANSPORT_MAX_QUEUED_FDS))
    {
        errno = ENOBUFS;
        return -1;
    }
    item = malloc(sizeof(*item) + bytes);
    if (item == NULL)
    {
        return -1;
    }
    item->fd = -1;
    if (fd >= 0)
    {
        item->fd = dup(fd);
        if (item->fd < 0 || fcntl(item->fd, F_SETFD, FD_CLOEXEC) < 0)
        {
            int saved_errno = errno;
            if (item->fd >= 0)
            {
                close(item->fd);
            }
            free(item);
            errno = saved_errno;
            return -1;
        }
        ++transport->queued_fds;
    }
    item->next = NULL;
    item->bytes = bytes;
    item->offset = 0;
    item->serial = ++transport->queued_serial;
    memcpy(item->data, data, bytes);
    if (transport->tail == NULL)
    {
        transport->head = item;
    }
    else
    {
        transport->tail->next = item;
    }
    transport->tail = item;
    transport->queued_bytes += bytes;
    ++transport->queued_items;
    return 0;
}

int
rdpTransportPending(const struct rdpTransport *transport)
{
    return transport->head != NULL;
}

int
rdpTransportFlush(struct rdpTransport *transport, int socket)
{
    unsigned int calls;
    size_t budget = TRANSPORT_IO_BYTES;
    int flags = 0;

#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
#ifdef MSG_DONTWAIT
    flags |= MSG_DONTWAIT;
#endif
    for (calls = 0; calls < TRANSPORT_IO_CALLS && budget > 0 &&
            transport->head != NULL; ++calls)
    {
        struct rdpTransportItem *item = transport->head;
        size_t count = item->bytes - item->offset;
        ssize_t sent;

        if (count > budget)
        {
            /* The peer receives the four-byte SCM_RIGHTS marker with one
             * recvmsg(). Do not split a small descriptor item merely because
             * earlier messages used almost all of this dispatch's budget. */
            if (item->fd >= 0 && count <= TRANSPORT_IO_BYTES)
            {
                break;
            }
            count = budget;
        }
        if (item->fd >= 0)
        {
            union
            {
                struct cmsghdr align;
                char data[CMSG_SPACE(sizeof(int))];
            } control;
            struct msghdr message = {0};
            struct iovec vector = {item->data + item->offset, count};
            struct cmsghdr *cmsg;

            memset(&control, 0, sizeof(control));
            message.msg_iov = &vector;
            message.msg_iovlen = 1;
            message.msg_control = control.data;
            message.msg_controllen = sizeof(control.data);
            cmsg = CMSG_FIRSTHDR(&message);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(cmsg), &item->fd, sizeof(int));
            sent = sendmsg(socket, &message, flags);
        }
        else
        {
            sent = send(socket, item->data + item->offset, count, flags);
        }
        if (sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return 0;
            }
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (sent == 0)
        {
            errno = EPIPE;
            return -1;
        }
        /* A successful sendmsg transferred the descriptor even if its data
         * was only partially written. Never attach that descriptor twice. */
        if (item->fd >= 0)
        {
            close(item->fd);
            item->fd = -1;
            --transport->queued_fds;
        }
        item->offset += sent;
        budget -= sent;
        transport->queued_bytes -= sent;
        if (item->offset == item->bytes)
        {
            transport->completed_serial = item->serial;
            transport->head = item->next;
            if (transport->head == NULL)
            {
                transport->tail = NULL;
            }
            --transport->queued_items;
            free(item);
        }
    }
    return 0;
}

int
rdpTransportReceive(struct rdpTransport *transport, int socket,
                    const uint8_t **data, size_t *bytes)
{
    unsigned int calls;
    size_t budget = TRANSPORT_IO_BYTES;
    int flags = 0;

#ifdef MSG_DONTWAIT
    flags |= MSG_DONTWAIT;
#endif
    for (calls = 0; calls < TRANSPORT_IO_CALLS && budget > 0; ++calls)
    {
        uint8_t *destination;
        size_t count;
        ssize_t received;

        if (transport->header_used < sizeof(transport->header))
        {
            destination = transport->header + transport->header_used;
            count = sizeof(transport->header) - transport->header_used;
        }
        else
        {
            destination = transport->input + transport->input_used;
            count = transport->input_expected - transport->input_used;
        }
        if (count > budget)
        {
            count = budget;
        }
        received = recv(socket, destination, count, flags);
        if (received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return 0;
            }
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (received == 0)
        {
            errno = ECONNRESET;
            return -1;
        }
        budget -= received;
        if (transport->header_used < sizeof(transport->header))
        {
            transport->header_used += received;
            if (transport->header_used == sizeof(transport->header))
            {
                uint32_t length = (uint32_t) transport->header[0] |
                                  (uint32_t) transport->header[1] << 8 |
                                  (uint32_t) transport->header[2] << 16 |
                                  (uint32_t) transport->header[3] << 24;
                /* Every message includes its length and a uint16 type. */
                if (length < 6 || length > RDP_TRANSPORT_MAX_MESSAGE_BYTES)
                {
                    errno = EMSGSIZE;
                    return -1;
                }
                transport->input_expected = length - 4;
                if (transport->input_size < transport->input_expected)
                {
                    uint8_t *input = realloc(transport->input,
                                             transport->input_expected);
                    if (input == NULL)
                    {
                        return -1;
                    }
                    transport->input = input;
                    transport->input_size = transport->input_expected;
                }
            }
        }
        else
        {
            transport->input_used += received;
            if (transport->input_used == transport->input_expected)
            {
                *data = transport->input;
                *bytes = transport->input_expected;
                transport->header_used = 0;
                transport->input_used = 0;
                transport->input_expected = 0;
                return 1;
            }
        }
    }
    return 0;
}

void
rdpTransportDestroy(struct rdpTransport *transport)
{
    struct rdpTransportItem *item = transport->head;
    while (item != NULL)
    {
        struct rdpTransportItem *next = item->next;
        if (item->fd >= 0)
        {
            close(item->fd);
        }
        free(item);
        item = next;
    }
    free(transport->input);
    memset(transport, 0, sizeof(*transport));
}
