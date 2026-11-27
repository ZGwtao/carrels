/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <libtrustedlo.h>

static inline bool service_manifest_check_type(protocon_svc_type_t svc_type)
{
    return (svc_type < SERVICE_RESERVED) && (svc_type > SERVICE_DUMMY);
}

static inline bool range_fits(uint64_t offset, uint64_t size, uint64_t capacity)
{
    return offset <= capacity && size <= capacity - offset;
}

static inline seL4_Error service_manifest_check(const service_manifest_header_t *header,
                                                size_t capacity)
{
    size_t expected_size;

    if (header->service_count > PROTOCON_MANIFEST_MAX_SERVICES) {
        return -1;
    }

    expected_size = sizeof(service_manifest_header_t) +
                    header->service_count * sizeof(service_manifest_entry_t);

    if (header->magic != MANIFEST_MAGIC || header->manifest_size != expected_size ||
        !range_fits(header->manifest_size, header->elf_size, capacity)) {
        return -1;
    }

    return seL4_NoError;
}

static inline void service_manifest_dump_entries(const service_manifest_header_t *header,
                                                 const service_manifest_entry_t *entries)
{
    for (uint32_t i = 0; i < header->service_count; ++i) {
        const service_manifest_entry_t *entry = &entries[i];

        TSLDR_DBG_PRINT("service[%d]: type=%d, offset=%x, size=%d\n",
                        i,
                        entry->type,
                        (unsigned long)entry->offset,
                        (unsigned long)entry->size);
    }
}

static inline uintptr_t service_manifest_retrieve_elf(service_manifest_header_t *header,
                                                      uintptr_t base)
{
    return (uintptr_t)((unsigned char *)(base) + header->manifest_size);
}

/* ----------------  Public Below  ------------------- */

static bool elf_header_is_valid(const Elf64_Ehdr *header, uint64_t elf_size)
{
    if (elf_size < sizeof(*header) || memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
        header->e_ident[EI_CLASS] != ELFCLASS64 ||
        header->e_ident[EI_DATA] != ELFDATA2LSB ||
        header->e_ident[EI_VERSION] != EV_CURRENT ||
        header->e_version != EV_CURRENT || header->e_ehsize != sizeof(*header)) {
        return false;
    }

    if (header->e_phnum != 0 &&
        (header->e_phentsize != sizeof(Elf64_Phdr) ||
         !range_fits(header->e_phoff,
                     (uint64_t)header->e_phnum * sizeof(Elf64_Phdr),
                     elf_size))) {
        return false;
    }

    if (header->e_shnum == 0 || header->e_shentsize != sizeof(Elf64_Shdr) ||
        header->e_shstrndx == SHN_UNDEF || header->e_shstrndx >= header->e_shnum ||
        !range_fits(header->e_shoff,
                    (uint64_t)header->e_shnum * sizeof(Elf64_Shdr),
                    elf_size)) {
        return false;
    }

    return true;
}

seL4_Error service_manifest_header_parse(payload_info_t *info,
                                         uintptr_t base,
                                         size_t capacity)
{
    service_manifest_header_t *header_manifest = (service_manifest_header_t *)base;
    service_manifest_entry_t *entries_start = NULL;
    uint32_t entries_num = 0;
    Elf64_Ehdr *header_payload = NULL;
    uint64_t elf_payload_size = 0;

    if (service_manifest_check(header_manifest, capacity)) {
        TSLDR_DBG_PRINT(PROGNAME "img format is not correct\n");
        return -1;
    }
    entries_num = header_manifest->service_count;
    entries_start = (service_manifest_entry_t *)((char *)base + sizeof(service_manifest_header_t));
    service_manifest_dump_entries(header_manifest, entries_start);

    header_payload = (Elf64_Ehdr *)service_manifest_retrieve_elf(header_manifest, base);
    if (!elf_header_is_valid(header_payload, header_manifest->elf_size)) {
        TSLDR_DBG_PRINT(PROGNAME "invalid ELF header or out-of-bounds ELF table\n");
        return -1;
    }

    elf_payload_size = header_manifest->elf_size;
    if (elf_payload_size == 0) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid elf size given for payload to be loaded\n");
        return -1;
    }

    info->header_payload = header_payload;
    info->elf_payload_size = elf_payload_size;
    info->service_count = entries_num;
    info->service_entries = entries_start;

    return seL4_NoError;
}

void service_manifest_parse(payload_info_t *info, protocon_svc_req_t *req)
{
    for (int i = 0; i < info->service_count; ++i) {
        const service_manifest_entry_t *entry = &info->service_entries[i];
        protocon_svc_type_t type = entry->type;
        if (service_manifest_check_type(type) != true) {
            TSLDR_DBG_PRINT("Unsupported service type: %d\n", type);
            continue;
        }
        req->service_count_per_type[type] += 1;
        req->service_entries[req->service_count] = entry;
        req->service_count += 1;
    }
    req->payload_e_entry = info->header_payload->e_entry;
}
