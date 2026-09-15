/*
 * SPDX-FileCopyrightText: 2026 UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lions/fs/config.h>

#define FS_MULTIPLEXER_MAX_CLIENTS 64
#define LIONS_FS_MULTIPLEXER_MAGIC_LEN 8

static const uint8_t LIONS_FS_MULTIPLEXER_MAGIC[LIONS_FS_MULTIPLEXER_MAGIC_LEN] =
    { 'L', 'i', 'o', 'n', 's', 'M', 'u', 0x1 };

typedef struct fs_multiplexer_config {
    uint8_t magic[LIONS_FS_MULTIPLEXER_MAGIC_LEN];
    fs_connection_resource_t server;
    fs_connection_resource_t clients[FS_MULTIPLEXER_MAX_CLIENTS];
    uint64_t num_clients;
} fs_multiplexer_config_t;

typedef struct fs_shared_server_config {
    uint8_t magic[LIONS_FS_MULTIPLEXER_MAGIC_LEN];
    fs_connection_resource_t multiplexer;
    region_resource_t client_shares[FS_MULTIPLEXER_MAX_CLIENTS];
    uint64_t num_clients;
} fs_shared_server_config_t;

static inline bool fs_multiplexer_config_check_magic(const void *config)
{
    const uint8_t *magic = config;
    for (unsigned i = 0; i < LIONS_FS_MULTIPLEXER_MAGIC_LEN; i++) {
        if (magic[i] != LIONS_FS_MULTIPLEXER_MAGIC[i]) {
            return false;
        }
    }
    return true;
}
