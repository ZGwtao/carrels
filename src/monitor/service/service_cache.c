/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <service/cache.h>

static uint8_t svc_cache_storage[SVC_CACHE_CAPACITY];
svc_cache_desc_t svc_cache;

const svc_cache_buf_t *svc_cache_find(const svc_cache_desc_t *cache,
                                      const svc_service_t *service)
{
    if (cache == NULL || service == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < cache->count; ++i) {
        if (cache->buffers[i].service == service) {
            return &cache->buffers[i];
        }
    }
    return NULL;
}

const svc_cache_buf_t *svc_cache_get_or_load(svc_cache_desc_t *cache,
                                             const svc_service_t *service)
{
    const svc_cache_buf_t *cached = svc_cache_find(cache, service);
    if (cached != NULL) {
        return cached;
    }
    if (cache == NULL || service == NULL || service->path_len == 0 ||
        service->path_len >= SVC_CACHE_MAX_PATH_LEN) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid service cache request\n");
        return NULL;
    }

    char path[SVC_CACHE_MAX_PATH_LEN];
    memcpy(path, svc_service_path(service), service->path_len);
    path[service->path_len] = '\0';

    if (cache->count == SVC_MAX_SERVICES) {
        memset(cache, 0, sizeof(*cache));
    }

    uint64_t size = 0;
    int err = pico_vfs_read_to_cache(svc_cache_storage + cache->used,
                                     sizeof(svc_cache_storage) - cache->used,
                                     path,
                                     &size);
    if (err == -2 && cache->used != 0) {
        TSLDR_DBG_PRINT(PROGNAME "Service cache full; clearing and retrying %s\n", path);
        memset(cache, 0, sizeof(*cache));
        err = pico_vfs_read_to_cache(svc_cache_storage,
                                     sizeof(svc_cache_storage),
                                     path,
                                     &size);
    }
    if (err != 0) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to cache service file %s\n", path);
        return NULL;
    }

    svc_cache_buf_t *entry = &cache->buffers[cache->count++];
    *entry = (svc_cache_buf_t){
        .service = service,
        .data = svc_cache_storage + cache->used,
        .size = (size_t)size,
    };
    cache->used += (size_t)size;
    TSLDR_DBG_PRINT(PROGNAME "Cached service file %s (%lu bytes)\n", path, size);
    return entry;
}
