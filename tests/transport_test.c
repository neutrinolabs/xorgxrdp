/*
 * Copyright 2026 xorgxrdp contributors
 * SPDX-License-Identifier: MIT
 *
 * Socket-level tests of the production transport, independent of Xorg.
 */

#include "rdpTransport.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (errno=%d)\n", \
                __FILE__, __LINE__, #condition, errno); \
        exit(1); \
    } \
} while (0)

static void
make_pair(int pair[2])
{
    int buffer_size = 4096;
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    CHECK(fcntl(pair[0], F_SETFL, O_NONBLOCK) == 0);
    CHECK(fcntl(pair[1], F_SETFL, O_NONBLOCK) == 0);
    CHECK(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF,
                     &buffer_size, sizeof(buffer_size)) == 0);
}

static void
test_blocked_and_partial_output(void)
{
    struct rdpTransport transport = {0};
    int pair[2];
    size_t length = 1024 * 1024;
    uint8_t *expected = malloc(length);
    uint8_t *received = malloc(length);
    size_t used = 0;
    size_t queued;
    size_t i;
    unsigned int loops;

    CHECK(expected != NULL && received != NULL);
    for (i = 0; i < length; ++i)
    {
        expected[i] = (uint8_t) (i * 17 + i / 251);
    }
    make_pair(pair);
    CHECK(rdpTransportQueue(&transport, expected, length, -1) == 0);
    CHECK(rdpTransportFlush(&transport, pair[0]) == 0);
    CHECK(rdpTransportPending(&transport));
    CHECK(transport.queued_bytes > 0 && transport.queued_bytes < length);
    queued = transport.queued_bytes;
    /* A peer that does not drain the socket must neither block nor drop data. */
    for (loops = 0; loops < 200; ++loops)
    {
        CHECK(rdpTransportFlush(&transport, pair[0]) == 0);
        CHECK(transport.queued_bytes == queued);
    }
    for (loops = 0; used < length && loops < 10000; ++loops)
    {
        ssize_t count = recv(pair[1], received + used, length - used, 0);
        if (count > 0)
        {
            used += count;
        }
        else
        {
            CHECK(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        }
        CHECK(rdpTransportFlush(&transport, pair[0]) == 0);
    }
    CHECK(used == length);
    CHECK(!rdpTransportPending(&transport));
    CHECK(memcmp(received, expected, length) == 0);
    CHECK(transport.queued_items == 0 && transport.queued_bytes == 0);
    rdpTransportDestroy(&transport);
    close(pair[0]);
    close(pair[1]);
    free(expected);
    free(received);
}

static void
test_descriptor_order_and_lifetime(void)
{
    struct rdpTransport transport = {0};
    int pair[2];
    int pipe_fds[2];
    int descriptor = -1;
    int descriptors = 0;
    size_t length = 1024 * 1024;
    size_t descriptor_length = 256 * 1024;
    int saw_partial_descriptor = 0;
    uint8_t *prefix = malloc(length);
    uint8_t *descriptor_data = malloc(descriptor_length);
    uint8_t *received = malloc(length + descriptor_length + 4);
    size_t used = 0;
    unsigned int loops;
    char marker;

    CHECK(prefix != NULL && descriptor_data != NULL && received != NULL);
    memset(descriptor_data, 0x39, descriptor_length);
    memcpy(descriptor_data, "int", 4);
    memset(prefix, 0x73, length);
    make_pair(pair);
    CHECK(pipe(pipe_fds) == 0);
    CHECK(write(pipe_fds[1], "x", 1) == 1);
    CHECK(rdpTransportQueue(&transport, prefix, length, -1) == 0);
    CHECK(rdpTransportQueue(&transport, descriptor_data, descriptor_length, pipe_fds[0]) == 0);
    CHECK(rdpTransportQueue(&transport, "tail", 4, -1) == 0);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    /* Also overwrite the caller's bytes: the queue must own its own copy. */
    memset(prefix, 0x00, length);
    CHECK(rdpTransportFlush(&transport, pair[0]) == 0);
    CHECK(transport.queued_fds == 1);

    for (loops = 0; used < length + descriptor_length + 4 && loops < 10000; ++loops)
    {
        union
        {
            struct cmsghdr align;
            char data[CMSG_SPACE(sizeof(int) * 2)];
        } control;
        struct msghdr message = {0};
        struct iovec vector = {received + used, length + descriptor_length + 4 - used};
        struct cmsghdr *cmsg;
        ssize_t count;

        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        message.msg_control = control.data;
        message.msg_controllen = sizeof(control.data);
        count = recvmsg(pair[1], &message, 0);
        if (count > 0)
        {
            used += count;
            CHECK(!(message.msg_flags & MSG_CTRUNC));
            for (cmsg = CMSG_FIRSTHDR(&message); cmsg != NULL;
                    cmsg = CMSG_NXTHDR(&message, cmsg))
            {
                CHECK(cmsg->cmsg_level == SOL_SOCKET);
                CHECK(cmsg->cmsg_type == SCM_RIGHTS);
                CHECK(cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
                memcpy(&descriptor, CMSG_DATA(cmsg), sizeof(descriptor));
                ++descriptors;
                /* Ancillary data cannot overtake the preceding bytes. */
                CHECK(used >= length + 1);
            }
        }
        else
        {
            CHECK(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        }
        if (descriptors == 1 && used < length + descriptor_length)
        {
            saw_partial_descriptor = 1;
        }
        CHECK(rdpTransportFlush(&transport, pair[0]) == 0);
    }
    CHECK(used == length + descriptor_length + 4);
    CHECK(descriptors == 1 && descriptor >= 0);
    CHECK(saw_partial_descriptor);
    CHECK(read(descriptor, &marker, 1) == 1 && marker == 'x');
    close(descriptor);
    memset(prefix, 0x73, length);
    CHECK(memcmp(received, prefix, length) == 0);
    CHECK(memcmp(received + length, descriptor_data, descriptor_length) == 0);
    CHECK(memcmp(received + length + descriptor_length, "tail", 4) == 0);
    CHECK(transport.queued_serial == 3 && transport.completed_serial == 3);
    CHECK(!rdpTransportPending(&transport) && transport.queued_fds == 0);
    rdpTransportDestroy(&transport);
    close(pair[0]);
    close(pair[1]);
    free(prefix);
    free(descriptor_data);
    free(received);
}

static void
test_descriptor_budget_boundary(void)
{
    unsigned int remaining;

    /* A protocol descriptor marker is four bytes. Exercise each insufficient
     * remainder of the production flush's 256 KiB dispatch budget. */
    for (remaining = 1; remaining <= 3; ++remaining)
    {
        struct rdpTransport transport = {0};
        int pair[2];
        int pipe_fds[2];
        int buffer_size = 1024 * 1024;
        int skip = 0;
        size_t length = 256 * 1024 - remaining;
        uint8_t *prefix = calloc(1, length);
        uint8_t *received = malloc(length);
        size_t used = 0;
        char marker[4];
        int descriptor = -1;
        union
        {
            struct cmsghdr align;
            char data[CMSG_SPACE(sizeof(int))];
        } control;
        struct msghdr message = {0};
        struct iovec vector = {marker, sizeof(marker)};
        struct cmsghdr *cmsg;

        CHECK(prefix != NULL && received != NULL);
        make_pair(pair);
        CHECK(pipe(pipe_fds) == 0);
        if (setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF,
                       &buffer_size, sizeof(buffer_size)) != 0 ||
                setsockopt(pair[1], SOL_SOCKET, SO_RCVBUF,
                           &buffer_size, sizeof(buffer_size)) != 0)
        {
            printf("SKIP descriptor budget boundary: cannot enlarge socket "
                   "buffers (%s)\n", strerror(errno));
            skip = 1;
            goto cleanup;
        }
        CHECK(rdpTransportQueue(&transport, prefix, length, -1) == 0);
        CHECK(rdpTransportQueue(&transport, "int", 4, pipe_fds[0]) == 0);
        CHECK(rdpTransportFlush(&transport, pair[0]) == 0);
        if (transport.completed_serial == 0)
        {
            puts("SKIP descriptor budget boundary: socket buffers cannot "
                 "hold the full 256 KiB prefix");
            skip = 1;
            goto cleanup;
        }
        CHECK(transport.completed_serial == 1);
        CHECK(transport.queued_bytes == 4 && transport.queued_fds == 1);
        while (used < length)
        {
            ssize_t count = recv(pair[1], received + used, length - used, 0);
            CHECK(count > 0);
            used += count;
        }
        CHECK(memcmp(received, prefix, length) == 0);
        CHECK(recv(pair[1], marker, sizeof(marker), 0) == -1 && errno == EAGAIN);
        CHECK(rdpTransportFlush(&transport, pair[0]) == 0);
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        message.msg_control = control.data;
        message.msg_controllen = sizeof(control.data);
        CHECK(recvmsg(pair[1], &message, 0) == 4);
        CHECK(memcmp(marker, "int", sizeof(marker)) == 0);
        cmsg = CMSG_FIRSTHDR(&message);
        CHECK(cmsg != NULL && cmsg->cmsg_level == SOL_SOCKET &&
              cmsg->cmsg_type == SCM_RIGHTS &&
              cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
        memcpy(&descriptor, CMSG_DATA(cmsg), sizeof(descriptor));
        CHECK(descriptor >= 0);
        close(descriptor);
        CHECK(!rdpTransportPending(&transport));
cleanup:
        rdpTransportDestroy(&transport);
        close(pair[0]);
        close(pair[1]);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        free(prefix);
        free(received);
        if (skip)
        {
            return;
        }
    }
}

static void
test_fragmented_input(void)
{
    struct rdpTransport transport = {0};
    int pair[2];
    const uint8_t message[] = {10, 0, 0, 0, 103, 0, 1, 2, 3, 4};
    const uint8_t *data = NULL;
    size_t bytes = 0;
    size_t i;

    make_pair(pair);
    CHECK(rdpTransportReceive(&transport, pair[0], &data, &bytes) == 0);
    for (i = 0; i < sizeof(message); ++i)
    {
        CHECK(send(pair[1], message + i, 1, 0) == 1);
        CHECK(rdpTransportReceive(&transport, pair[0], &data, &bytes) ==
              (i + 1 == sizeof(message)));
    }
    CHECK(bytes == sizeof(message) - 4);
    CHECK(memcmp(data, message + 4, bytes) == 0);
    /* Coalesced messages remain separately framed and in order. */
    CHECK(send(pair[1], message, sizeof(message), 0) == sizeof(message));
    CHECK(send(pair[1], message, sizeof(message), 0) == sizeof(message));
    for (i = 0; i < 2; ++i)
    {
        CHECK(rdpTransportReceive(&transport, pair[0], &data, &bytes) == 1);
        CHECK(bytes == sizeof(message) - 4);
        CHECK(memcmp(data, message + 4, bytes) == 0);
    }
    CHECK(rdpTransportReceive(&transport, pair[0], &data, &bytes) == 0);
    CHECK(send(pair[1], message, 5, 0) == 5);
    CHECK(rdpTransportReceive(&transport, pair[0], &data, &bytes) == 0);
    close(pair[1]);
    CHECK(rdpTransportReceive(&transport, pair[0], &data, &bytes) == -1);
    rdpTransportDestroy(&transport);
    close(pair[0]);
}

static void
test_invalid_lengths(void)
{
    const uint32_t lengths[] = {0, 4, 5, RDP_TRANSPORT_MAX_MESSAGE_BYTES + 1,
                                UINT32_MAX};
    size_t i;
    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
    {
        struct rdpTransport transport = {0};
        int pair[2];
        const uint8_t *data;
        size_t bytes;
        uint8_t header[4] = {lengths[i], lengths[i] >> 8,
                             lengths[i] >> 16, lengths[i] >> 24};
        make_pair(pair);
        CHECK(send(pair[1], header, sizeof(header), 0) == sizeof(header));
        CHECK(rdpTransportReceive(&transport, pair[0], &data, &bytes) == -1);
        CHECK(errno == EMSGSIZE && transport.input == NULL);
        rdpTransportDestroy(&transport);
        close(pair[0]);
        close(pair[1]);
    }
}

static void
test_limits_and_cleanup(void)
{
    struct rdpTransport transport = {0};
    int pair[2];
    int pipe_fds[2];
    char byte;
    unsigned int i;
    char *large = malloc(RDP_TRANSPORT_MAX_QUEUED_BYTES);

    CHECK(large != NULL);
    memset(large, 0x12, RDP_TRANSPORT_MAX_QUEUED_BYTES);
    CHECK(rdpTransportQueue(&transport, large,
                            RDP_TRANSPORT_MAX_QUEUED_BYTES, -1) == 0);
    CHECK(rdpTransportQueue(&transport, "x", 1, -1) == -1 && errno == ENOBUFS);
    rdpTransportDestroy(&transport);
    free(large);
    for (i = 0; i < RDP_TRANSPORT_MAX_QUEUED_ITEMS; ++i)
    {
        CHECK(rdpTransportQueue(&transport, "x", 1, -1) == 0);
    }
    CHECK(rdpTransportQueue(&transport, "x", 1, -1) == -1 && errno == ENOBUFS);
    rdpTransportDestroy(&transport);

    CHECK(pipe(pipe_fds) == 0);
    CHECK(fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) == 0);
    for (i = 0; i < RDP_TRANSPORT_MAX_QUEUED_FDS; ++i)
    {
        CHECK(rdpTransportQueue(&transport, "int", 4, pipe_fds[1]) == 0);
    }
    CHECK(rdpTransportQueue(&transport, "int", 4, pipe_fds[1]) == -1 &&
          errno == ENOBUFS);
    close(pipe_fds[1]);
    CHECK(read(pipe_fds[0], &byte, 1) == -1 && errno == EAGAIN);
    rdpTransportDestroy(&transport);
    CHECK(read(pipe_fds[0], &byte, 1) == 0); /* Every queued duplicate closed. */
    close(pipe_fds[0]);

    make_pair(pair);
    close(pair[1]);
    CHECK(rdpTransportQueue(&transport, "tail", 4, -1) == 0);
    CHECK(rdpTransportFlush(&transport, pair[0]) == -1);
    CHECK(rdpTransportPending(&transport));
    rdpTransportDestroy(&transport);
    close(pair[0]);
}

int
main(void)
{
    signal(SIGPIPE, SIG_IGN);
    /* Catch accidental blocking regressions without imposing tight timings. */
    alarm(10);
    test_blocked_and_partial_output();
    test_descriptor_order_and_lifetime();
    test_descriptor_budget_boundary();
    test_fragmented_input();
    test_invalid_lengths();
    test_limits_and_cleanup();
    alarm(0);
    puts("transport tests passed");
    return 0;
}
