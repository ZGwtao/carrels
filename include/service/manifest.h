#pragma once

#include <sel4/sel4.h>
#include <elf.h>

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

