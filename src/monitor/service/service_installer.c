/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <libtrustedlo.h>

static seL4_Word
service_installer_elf_get_sec_with_vaddr(const void *elf_base, uintptr_t vaddr, seL4_Word *sh_size)
{
    const uint8_t *base = (const uint8_t *)elf_base;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(base + eh->e_shoff);

    for (uint16_t i = 0; i < eh->e_shnum; ++i) {
        seL4_Word start = sh[i].sh_addr;
        seL4_Word size = sh[i].sh_size;
        if (vaddr >= start && vaddr < start + size) {
            if (sh[i].sh_type == SHT_NOBITS) {
                break;
            }
            if (sh_size) {
                *sh_size = size - (vaddr - start);
            }
            return (seL4_Word)(elf_base + sh[i].sh_offset + (vaddr - start));
        }
    }
    return (seL4_Word)-1;
}

static bool service_installer_payload_add_service(const void *elf_base,
                                                  const svc_cache_buf_t *cached,
                                                  uintptr_t vaddr,
                                                  uint64_t manifest_size)
{
    seL4_Word available = 0;
    seL4_Word target = service_installer_elf_get_sec_with_vaddr(elf_base, vaddr, &available);
    if (target == (seL4_Word)-1 || cached->size > available || cached->size > manifest_size) {
        TSLDR_DBG_PRINT(PROGNAME "Service data does not fit target section at %lx\n", vaddr);
        return false;
    }
    memcpy((void *)target, cached->data, cached->size);
    return true;
}

static inline void service_installer_append_acrtreq(trustedlo_xrtreq_t *xrt_req_list,
                                                    const svc_service_t *svc)
{
    for (uint32_t i = 0; i < svc->resource_count; i++) {
        const svc_resource_t *resource = svc_service_resource(svc, i);

        switch (resource->kind) {
        case SVC_RESOURCE_CHANNEL_NOTIFY: {
            if (xrt_req_list->num_req_notifications >= 64) {
                break;
            }
            seL4_Word idx = xrt_req_list->num_req_notifications++;
            xrt_req_list->notifications[idx] = (seL4_Word)resource->value;
            break;
        }

        case SVC_RESOURCE_CHANNEL_PPC: {
            if (xrt_req_list->num_req_ppcs >= 64) {
                break;
            }
            seL4_Word idx = xrt_req_list->num_req_ppcs++;
            xrt_req_list->ppcs[idx] = (seL4_Word)resource->value;
            break;
        }

        case SVC_RESOURCE_MAP: {
            if (xrt_req_list->num_req_mappings >= 64) {
                break;
            }
            seL4_Word idx = xrt_req_list->num_req_mappings++;
            xrt_req_list->mappings[idx] = (seL4_Word)resource->value;
            break;
        }

        default:
            break;
        }
    }
}

static inline void
service_installer_initialise_ac_rt_req_header(void *xrt_req_header,
                                              const trustedlo_xrtreq_t *xrt_req_list)
{
    trustedlo_xrtreq_header_t *header = (trustedlo_xrtreq_header_t *)(xrt_req_header);

    header->total_num = xrt_req_list->num_req_notifications + xrt_req_list->num_req_ppcs +
                        xrt_req_list->num_req_ioports + xrt_req_list->num_req_mappings +
                        xrt_req_list->num_req_irqs;

    header->serialised_offset = sizeof(trustedlo_xrtreq_header_t);

    mktxlo_prepare_xrt_req_list((char *)(header) + header->serialised_offset, xrt_req_list);
}

static inline void *protocon_image_elf(uintptr_t image_base)
{
    const protocon_image_header_t *header = (const protocon_image_header_t *)image_base;
    return (void *)(image_base + header->elf_offset);
}

bool service_installer_apply(const deploy_plan_t *plan)
{
    trustedlo_xrtreq_t xrt_req_list = {};
    void *elf_base = protocon_image_elf(plan->pc_base);

    for (uint32_t i = 0; i < plan->req->service_count; ++i) {
        const protocon_svc_req_t *req = plan->req;
        const svc_service_t *curr_svc = plan->service_sources[i];

        const svc_cache_buf_t *cached = svc_cache_get_or_load(&svc_cache, curr_svc);
        if (cached == NULL) {
            TSLDR_DBG_PRINT(PROGNAME "Missing cached service data\n");
            return false;
        }

        TSLDR_DBG_PRINT(PROGNAME "image_base: %x, elf_base: %x, service vaddr: %x\n",
                        (uintptr_t)plan->pc_base,
                        (uintptr_t)elf_base,
                        (uintptr_t)req->service_entries[i]->offset +
                            (uintptr_t)req->payload_e_entry);

        if (!service_installer_payload_add_service(
                elf_base,
                cached,
                (uintptr_t)req->service_entries[i]->offset + (uintptr_t)req->payload_e_entry,
                req->service_entries[i]->size)) {
            return false;
        }

        service_installer_append_acrtreq(&xrt_req_list, curr_svc);
    }

    service_installer_initialise_ac_rt_req_header((char *)plan->base_serialised_service,
                                                  &xrt_req_list);
    return true;
}
