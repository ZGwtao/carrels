/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <sel4/sel4.h>
#include <elf.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    uint64_t magic;
    uint32_t service_count;
    uint32_t manifest_size;
    uint64_t elf_size;
} service_manifest_header_t;

#define MANIFEST_MAGIC UINT64_C(0x504353564D414E31)

typedef struct __attribute__((packed)) {
    int32_t type;
    uint64_t offset;
    uint64_t size;
} service_manifest_entry_t;

_Static_assert(sizeof(service_manifest_header_t) == 24,
               "Invalid manifest header size");
_Static_assert(sizeof(service_manifest_entry_t) == 20,
               "Invalid manifest entry size");

typedef struct {
    Elf64_Ehdr *header_payload;
    uint64_t elf_payload_size;
    uint32_t service_count;
    service_manifest_entry_t *service_entries;
} payload_info_t;


typedef struct {
    uint32_t service_count;
    uint32_t service_count_per_type[SVC_TYPE_MAX_NUM];
    service_manifest_entry_t *service_entries[16];
    Elf64_Addr payload_e_entry;
} protocon_svc_req_t;


typedef enum {
    SVC_RESOURCE_CHANNEL_NOTIFY = 1,
    SVC_RESOURCE_CHANNEL_PPC = 2,
    SVC_RESOURCE_MAP = 3,
} svc_resource_kind_t;

#define SVC_MAX_SERVICES 64
#define SVC_HEADER_SIZE 20
#define SVC_SERVICE_HEADER_SIZE 22
#define SVC_RESOURCE_SIZE 9
#define SVC_VERSION 1

typedef struct __attribute__((packed)) {
    uint8_t kind;
    uint64_t value;
} svc_resource_t;

typedef struct __attribute__((packed)) {
    uint32_t record_size;
    uint64_t pd_id;
    uint8_t service_id;
    uint8_t service_type;
    uint32_t resource_count;
    uint32_t path_len;
} svc_service_t;

typedef struct {
    uint16_t version;
    uint32_t service_count;
    uint32_t total_size;
    const svc_service_t *services[SVC_MAX_SERVICES];
} svc_t;

static inline uint16_t svc_read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t svc_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline const svc_resource_t *svc_service_resource(const svc_service_t *service, uint32_t index)
{
    if (index >= service->resource_count) return NULL;
    return (const svc_resource_t *)((const uint8_t *)service + SVC_SERVICE_HEADER_SIZE + index * SVC_RESOURCE_SIZE);
}

static inline const char *svc_service_path(const svc_service_t *service)
{
    return (const char *)service + SVC_SERVICE_HEADER_SIZE + service->resource_count * SVC_RESOURCE_SIZE;
}

static inline bool svc_parse(const void *base, svc_t *svc)
{
    const uint8_t *start = base;
    const uint8_t *p = start;
    static const uint8_t magic[8] = { 'O', 'S', 'S', 'v', 'c', 0, 0, 0 };

    if (memcmp(p, magic, sizeof(magic)) != 0) return false;

    svc->version = svc_read_u16(p + 8);
    svc->service_count = svc_read_u32(p + 12);
    svc->total_size = svc_read_u32(p + 16);
    if (svc->version != SVC_VERSION || svc->service_count > SVC_MAX_SERVICES || svc->total_size < SVC_HEADER_SIZE) return false;

    p += SVC_HEADER_SIZE;

    for (uint32_t i = 0; i < svc->service_count; i++) {
        if (p + SVC_SERVICE_HEADER_SIZE > start + svc->total_size) return false;

        uint32_t record_size = svc_read_u32(p);
        uint32_t resource_count = svc_read_u32(p + 14);
        uint32_t path_len = svc_read_u32(p + 18);
        uint32_t expected_size = SVC_SERVICE_HEADER_SIZE + resource_count * SVC_RESOURCE_SIZE + path_len;

        if (record_size != expected_size || p + record_size > start + svc->total_size) return false;

        svc->services[i] = (const svc_service_t *)p;
        p += record_size;
    }

    return p == start + svc->total_size;
}
