
#include <carrels-monitor.h>
#include <libtrustedlo.h>


static inline bool
service_manifest_check_type(protocon_svc_type_t svc_type)
{
    return (svc_type < SERVICE_RESERVED) &&
            (svc_type > SERVICE_DUMMY);
}

static inline seL4_Error
service_manifest_check(service_manifest_header_t *header)
{
    size_t expected_size =
        sizeof(service_manifest_header_t) +
        header->service_count * sizeof(service_manifest_entry_t);

    if (header->magic != MANIFEST_MAGIC ||
        header->manifest_size != expected_size) {
        return -1;
    }

    return seL4_NoError;
}

static inline void
service_manifest_dump_entries(
    const service_manifest_header_t *header,
    const service_manifest_entry_t *entries
)
{
    for (uint32_t i = 0; i < header->service_count; ++i) {
        const service_manifest_entry_t *entry = &entries[i];

        TSLDR_DBG_PRINT(
            "service[%d]: type=%d, offset=%x, size=%d\n",
            i,
            entry->type,
            (unsigned long)entry->offset,
            (unsigned long)entry->size
        );
    }
}

static inline uintptr_t
service_manifest_retrieve_elf(service_manifest_header_t *header, uintptr_t base)
{
    return (uintptr_t)((unsigned char *)(base) + header->manifest_size);
}

/* ----------------  Public Below  ------------------- */

seL4_Error
service_manifest_header_parse(payload_info_t *info, uintptr_t base)
{
    service_manifest_header_t *header_manifest = (service_manifest_header_t *)base;
    service_manifest_entry_t *entries_start = NULL;
    uint32_t entries_num = 0;
    Elf64_Ehdr *header_payload = NULL;
    uint64_t elf_payload_size = 0;

    if (service_manifest_check(header_manifest)) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "img format is not correct\n"
        );
        return -1;
    }
    entries_num = header_manifest->service_count;
    entries_start = (service_manifest_entry_t *)(
        (char *)base + sizeof(service_manifest_header_t)
    );
    service_manifest_dump_entries(header_manifest, entries_start);

    header_payload = (Elf64_Ehdr *)service_manifest_retrieve_elf(header_manifest, base);
    if (header_payload->e_shoff == 0 ||
        header_payload->e_shnum == 0 ||
        header_payload->e_shentsize != sizeof(Elf64_Shdr) ||
        header_payload->e_shstrndx == SHN_UNDEF ||
        header_payload->e_shstrndx >= header_payload->e_shnum)
    {
        TSLDR_DBG_PRINT(
            PROGNAME
            "no section headers present or unexpected shentsize "
            "or invalid e_shstrndx\n"
        );
        return -1;
    }

    elf_payload_size = header_manifest->elf_size;
    if (elf_payload_size == 0) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Invalid elf size given for payload to be loaded\n"
        );
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
        const service_manifest_entry_t *entry =
                            &info->service_entries[i];
        protocon_svc_type_t type = entry->type;
        if (service_manifest_check_type(type) != true) {
            TSLDR_DBG_PRINT(
                "Unsupported service type: %d\n",
                type
            );
            continue;
        }
        req->service_count_per_type[type] += 1;
        req->service_entries[req->service_count] = entry;
        req->service_count += 1;
    }
    req->payload_e_entry = info->header_payload->e_entry;
}

