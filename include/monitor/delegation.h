/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define DLG_MAX_DELEGATORS 16
#define DLG_HEADER_SIZE 16
#define DLG_DELEGATOR_HEADER_SIZE 16
#define DLG_RESOURCE_SIZE 21

typedef struct __attribute__((packed)) {
    uint8_t kind;
    uint8_t flags;
    uint16_t slot;
    uint8_t cap_count;
    uint64_t arg0;
    uint64_t arg1;
} dlg_resource_t;

typedef struct __attribute__((packed)) {
    uint16_t record_size;
    uint16_t resource_count;
    uint32_t delegation_cap;
    uint64_t pd_id;
} dlg_delegator_t;

typedef struct {
    uint32_t delegator_count;
    uint32_t total_size;
    const dlg_delegator_t *delegators[DLG_MAX_DELEGATORS];
} dlg_header_t;

static inline uint16_t dlg_read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t dlg_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline const dlg_resource_t *dlg_delegator_resource(const dlg_delegator_t *delegator, uint16_t index)
{
    if (index >= delegator->resource_count) return NULL;
    return (const dlg_resource_t *)((const uint8_t *)delegator + DLG_DELEGATOR_HEADER_SIZE + index * DLG_RESOURCE_SIZE);
}

static inline const dlg_delegator_t *dlg_find_delegator(const dlg_header_t *dlg, uint64_t pd_id)
{
    for (uint32_t i = 0; i < dlg->delegator_count; i++) {
        if (dlg->delegators[i]->pd_id == pd_id) return dlg->delegators[i];
    }
    return NULL;
}

static inline bool dlg_parse(const void *base, dlg_header_t *dlg)
{
    const uint8_t *start = base;
    const uint8_t *p = start;
    static const uint8_t magic[8] = { 'C', 'a', 'p', 'D', 'e', 'l', 'g', 0 };

    if (memcmp(p, magic, sizeof(magic)) != 0) return false;

    dlg->delegator_count = dlg_read_u32(p + 8);
    dlg->total_size = dlg_read_u32(p + 12);
    if (dlg->delegator_count > DLG_MAX_DELEGATORS || dlg->total_size < DLG_HEADER_SIZE) return false;

    p += DLG_HEADER_SIZE;

    for (uint32_t i = 0; i < dlg->delegator_count; i++) {
        if (p + DLG_DELEGATOR_HEADER_SIZE > start + dlg->total_size) return false;

        uint16_t record_size = dlg_read_u16(p);
        uint16_t resource_count = dlg_read_u16(p + 2);
        if (record_size < DLG_DELEGATOR_HEADER_SIZE || record_size != DLG_DELEGATOR_HEADER_SIZE + resource_count * DLG_RESOURCE_SIZE) return false;
        if (p + record_size > start + dlg->total_size) return false;

        dlg->delegators[i] = (const dlg_delegator_t *)p;
        p += record_size;
    }

    return p == start + dlg->total_size;
}
