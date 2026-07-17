
#include <ossvc.h>
#include <protocon.h>
#include <libtrustedlo.h>

#include <lions/fs/config.h>
#include <pico_vfs.h>

static inline void
service_installer_payload_add_service(const void *elf_base, const char data_file[], uintptr_t vaddr)
{
    int err = 0;
    seL4_Word target_sh = tsldr_miscutil_fetch_elf_section_with_vaddr(elf_base, vaddr, NULL);
    if (!target_sh) {
        // the reason we allow early return in here is:
        //  a broken client program will only break a dynamic PD's execution
        //  we can still load a broken elf into a dynamic PD but keep the rest of the system safe
        // so, if unfortunately the client breaks something in its user-defined section
        // it is none of the monitor or dynamic PD's business, as we just need to restore a faulting PD...
        TSLDR_DBG_PRINT(LIB_NAME_MACRO "Failed to find the target section (vaddr '%x') to patch with\n", vaddr);
        return;
    }
    pico_vfs_readfile2buf((void *)target_sh, data_file, &err);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(LIB_NAME_MACRO "Failed to patch payload with datafile '%s' at: %x", data_file, vaddr);
        // FIXME: we do nothing here, but should it behave like this?
    }
}


static inline bool
service_installer_check_svc(const protocon_svc_t *svc)
{
    if (svc->svc_init != true) {
        return false;
    }
    return true;
}

static inline void
service_installer_append_acrtreq(tsldr_acrtreq_t *req_acrt, const protocon_svc_t *svc)
{
    // maximumlly, we allow each OS svc to have at most:
    //  - 4 notifications
    //  - 4 ppcs
    //  - 4 irqs (not implemented here)
    //  - *4 x86ioports (not implemented in microkit)
    //  - 4 mappings (4 pieaces of memory regions)
    // these low-level access rights should be enough to describe an OS service
    for (int i = 0; i < 4; ++i) {
        if (svc->ppcs[i] >= MICROKIT_MAX_CHANNELS) {
            continue;
        }
        seL4_Word ppc = req_acrt->num_req_ppcs;
        req_acrt->ppcs[ppc] = (seL4_Word)svc->ppcs[i];
        req_acrt->num_req_ppcs++;
    }
    for (int i = 0; i < 4; ++i) {
        if (svc->notifications[i] >= MICROKIT_MAX_CHANNELS) {
            continue;
        }
        seL4_Word ntfn = req_acrt->num_req_notifications;
        req_acrt->notifications[ntfn] = (seL4_Word)svc->notifications[i];
        req_acrt->num_req_notifications++;
    }
    /* TODO: irq, and x86ioports... */
    for (int i = 0; i < 4; ++i) {
        if (!svc->mappings[i].vaddr) {
            continue;
        }
        seL4_Word mapping = req_acrt->num_req_mappings;
        req_acrt->mappings[mapping] = (seL4_Word)svc->mappings[i].vaddr;
        req_acrt->num_req_mappings++;
    }
}


static inline void
service_installer_initialise_AcRtReqHeader(
    void *header_base,
    const tsldr_acrtreq_t *req
)
{
    tsldr_acrtreq_header_t *header = (tsldr_acrtreq_header_t *)(header_base);

    header->total_num = 
                req->num_req_notifications + \
                req->num_req_ppcs + \
                req->num_req_ioports + \
                req->num_req_mappings + \
                req->num_req_irqs;

    header->serialised_offset = sizeof(tsldr_acrtreq_header_t);

    tsldr_main_monitor_encode_required_rights(
        (char *)(header_base) + header->serialised_offset,
        req
    );
}



void service_installer_apply(const deploy_plan_t *plan)
{
    // the request variable, which should be filled out with the low-level access rights information
    // we use it to record the access rights of the required OS services (i.e., svcs from above)
    // we will then send this thing to the trusted loading functions for actual trusted loading
    // the reason we need it is that the trusted loader does not handle high-level information
    // so we put an information flow transition that turns requested OS services into low-level details
    tsldr_acrtreq_t req_acrt = {};

    for (uint32_t i = 0;
         i < plan->req->service_count;
         ++i)
    {
        const protocon_svc_req_t *req = plan->req;
        const protocon_svc_t *curr_svc =
                        plan->service_sources[i];

        TSLDR_DBG_PRINT(
            PROGNAME
            "pc_base: %x, service vaddr: %x, datapath: %s\n",
            (uintptr_t)(plan->pc_base),
            (uintptr_t)(req->service_entries[i]->offset) + (uintptr_t)(req->payload_e_entry),
            curr_svc->data_path
        );


        service_installer_payload_add_service(
            (void *)(plan->pc_base),
            curr_svc->data_path,
            (uintptr_t)(req->service_entries[i]->offset) + (uintptr_t)(req->payload_e_entry)
        );
        service_installer_append_acrtreq(&req_acrt, curr_svc);
    }

    service_installer_initialise_AcRtReqHeader(
        (char *)(plan->base_serialised_service),
        &req_acrt
    );
}
