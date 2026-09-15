/*
 * SPDX-FileCopyrightText: 2026 UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/fs/protocol.h>

/* Internal protocol between an FS multiplexer and a shared FS server. */
typedef struct fs_mux_cmd {
    uint64_t client_id;
    fs_cmd_t command;
} fs_mux_cmd_t;

typedef struct fs_mux_cmpl {
    uint64_t client_id;
    fs_cmpl_t completion;
} fs_mux_cmpl_t;

typedef union fs_mux_msg {
    fs_mux_cmd_t cmd;
    fs_mux_cmpl_t cmpl;
} fs_mux_msg_t;

_Static_assert(sizeof(fs_mux_msg_t) == 72,
               "fs_mux_msg_t must be exactly 72 bytes");

typedef struct fs_mux_queue {
    uint64_t head;
    uint64_t tail;
    uint8_t padding[48];
    fs_mux_msg_t buffer[FS_QUEUE_CAPACITY];
} fs_mux_queue_t;

static inline uint64_t fs_mux_queue_length_consumer(fs_mux_queue_t *queue)
{
    return __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE) - queue->head;
}

static inline uint64_t fs_mux_queue_length_producer(fs_mux_queue_t *queue)
{
    return queue->tail - __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE);
}

static inline fs_mux_msg_t *fs_mux_queue_idx_filled(fs_mux_queue_t *queue,
                                                     uint64_t index)
{
    return &queue->buffer[(queue->head + index) % FS_QUEUE_CAPACITY];
}

static inline fs_mux_msg_t *fs_mux_queue_idx_empty(fs_mux_queue_t *queue,
                                                    uint64_t index)
{
    return &queue->buffer[(queue->tail + index) % FS_QUEUE_CAPACITY];
}

static inline void fs_mux_queue_publish_consumption(fs_mux_queue_t *queue,
                                                     uint64_t amount)
{
    __atomic_store_n(&queue->head, queue->head + amount, __ATOMIC_RELEASE);
}

static inline void fs_mux_queue_publish_production(fs_mux_queue_t *queue,
                                                    uint64_t amount)
{
    __atomic_store_n(&queue->tail, queue->tail + amount, __ATOMIC_RELEASE);
}
