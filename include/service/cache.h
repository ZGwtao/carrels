/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <service/manifest.h>

#define SVC_CACHE_CAPACITY (256U * 1024U)
#define SVC_CACHE_MAX_PATH_LEN 128U

typedef struct {
    const svc_service_t *service;
    const uint8_t *data;
    size_t size;
} svc_cache_buf_t;

typedef struct {
    svc_cache_buf_t buffers[SVC_MAX_SERVICES];
    uint32_t count;
    size_t used;
} svc_cache_desc_t;

extern svc_cache_desc_t svc_cache;

const svc_cache_buf_t *svc_cache_find(const svc_cache_desc_t *cache,
                                      const svc_service_t *service);
const svc_cache_buf_t *svc_cache_get_or_load(svc_cache_desc_t *cache,
                                             const svc_service_t *service);
