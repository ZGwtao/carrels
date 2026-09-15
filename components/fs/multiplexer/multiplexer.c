/*
 * SPDX-FileCopyrightText: 2026 UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <microkit.h>
#include <lions/fs/multiplexer.h>
#include <lions/fs/multiplexer_config.h>

__attribute__((section(".fs_multiplexer_config")))
fs_multiplexer_config_t fs_multiplexer_config;

static fs_mux_queue_t *server_commands;
static fs_mux_queue_t *server_completions;

static int client_from_channel(microkit_channel ch)
{
    for (uint64_t i = 0; i < fs_multiplexer_config.num_clients; i++) {
        if (fs_multiplexer_config.clients[i].id == ch) {
            return (int)i;
        }
    }
    return -1;
}

void init(void)
{
    assert(fs_multiplexer_config_check_magic(&fs_multiplexer_config));
    assert(fs_multiplexer_config.num_clients > 0);
    assert(fs_multiplexer_config.num_clients <= FS_MULTIPLEXER_MAX_CLIENTS);
    server_commands = fs_multiplexer_config.server.command_queue.vaddr;
    server_completions = fs_multiplexer_config.server.completion_queue.vaddr;
}

static void forward_client_requests(uint64_t client_id)
{
    fs_connection_resource_t *client = &fs_multiplexer_config.clients[client_id];
    fs_queue_t *commands = client->command_queue.vaddr;
    uint64_t available = FS_QUEUE_CAPACITY -
        fs_mux_queue_length_producer(server_commands);
    uint64_t count = fs_queue_length_consumer(commands);
    if (count > available) count = available;

    for (uint64_t i = 0; i < count; i++) {
        fs_mux_msg_t *out = fs_mux_queue_idx_empty(server_commands, i);
        out->cmd.client_id = client_id;
        out->cmd.command = fs_queue_idx_filled(commands, i)->cmd;
    }
    if (count) {
        fs_queue_publish_consumption(commands, count);
        fs_mux_queue_publish_production(server_commands, count);
        microkit_notify(fs_multiplexer_config.server.id);
    }
}

static void forward_server_completions(void)
{
    uint64_t available[FS_MULTIPLEXER_MAX_CLIENTS];
    uint64_t produced[FS_MULTIPLEXER_MAX_CLIENTS] = {0};
    for (uint64_t i = 0; i < fs_multiplexer_config.num_clients; i++) {
        fs_queue_t *queue = fs_multiplexer_config.clients[i].completion_queue.vaddr;
        available[i] = FS_QUEUE_CAPACITY - fs_queue_length_producer(queue);
    }

    uint64_t consumed = 0;
    uint64_t count = fs_mux_queue_length_consumer(server_completions);
    while (consumed < count) {
        fs_mux_msg_t *in = fs_mux_queue_idx_filled(server_completions, consumed);
        uint64_t client_id = in->cmpl.client_id;
        assert(client_id < fs_multiplexer_config.num_clients);
        if (produced[client_id] == available[client_id]) break;
        fs_queue_t *queue = fs_multiplexer_config.clients[client_id].completion_queue.vaddr;
        fs_queue_idx_empty(queue, produced[client_id]++)->cmpl = in->cmpl.completion;
        consumed++;
    }
    if (consumed) fs_mux_queue_publish_consumption(server_completions, consumed);
    for (uint64_t i = 0; i < fs_multiplexer_config.num_clients; i++) {
        if (produced[i]) {
            fs_queue_t *queue = fs_multiplexer_config.clients[i].completion_queue.vaddr;
            fs_queue_publish_production(queue, produced[i]);
            microkit_notify(fs_multiplexer_config.clients[i].id);
        }
        /* A completion frees a server slot; retry clients held by backpressure. */
        forward_client_requests(i);
    }
}

void notified(microkit_channel ch)
{
    if (ch == fs_multiplexer_config.server.id) {
        forward_server_completions();
        return;
    }
    int client_id = client_from_channel(ch);
    if (client_id >= 0) forward_client_requests((uint64_t)client_id);
}
