
#include <ossvc.h>
#include <protocon.h>
#include <libtrustedlo.h>

#include <lions/fs/config.h>
#include <pico_vfs.h>

void monitor_worker_func__patch_payload_by_ptr(const void *elf_base, const char data_file[], uintptr_t vaddr)
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

static inline void
service_installer_apply_one(
    const protocon_svc_t *svc,
    protocon_svc_req_t *cursor,
    tsldr_acrtreq_t *req_acrt,
    const uintptr_t payload_base
) {
    if (!svc->svc_init) {
        return;
    }
    uint8_t type = svc->svc_type;

    if (!cursor->num_svc_per_type[type]) {
        return;
    }

    uintptr_t target_section;

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

    target_section =
        cursor->data_per_svc_instance[type]
                                     [cursor->num_svc_per_type[type] - 1];

    monitor_worker_func__patch_payload_by_ptr(
        (void *)payload_base,
        svc->data_path,
        (uintptr_t)(target_section)
    );

    cursor->num_svc_per_type[type]--;
}


void service_installer_apply(
    const deploy_plan_t *plan,
    const protocon_svc_req_t *req,
    uintptr_t monitor_svcdb_base,
    const protocon_svcdb_t *svcdb
) {

    TSLDR_DBG_PRINT(LIB_NAME_MACRO "pd index of the given os svcdb: %d\n", svcdb->pd_idx);
    TSLDR_DBG_PRINT(LIB_NAME_MACRO "number of svcs in the os svcdb: %d\n", svcdb->svc_num);

    // the request variable, which should be filled out with the low-level access rights information
    // we use it to record the access rights of the required OS services (i.e., svcs from above)
    // we will then send this thing to the trusted loading functions for actual trusted loading
    // the reason we need it is that the trusted loader does not handle high-level information
    // so we put an information flow transition that turns requested OS services into low-level details
    tsldr_acrtreq_t req_acrt = {};
    protocon_svc_req_t cursor;
    const protocon_svc_t *curr_svc;
    seL4_Word *svc_num_ptr = NULL;
    unsigned char *svc_data_ptr = NULL;

    tsldr_miscutil_memcpy(&cursor, req, sizeof(cursor));

    curr_svc = svcdb->array;
    // check all available os services, and patch each of them accordingly
    //  - patch the elf with information that describes the required os services
    // this is a part of the process of elf preparation
    for (int i = 0; i < svcdb->svc_num; ++i) {
        // basically it is similar to tell the client program where to access the OS services
        // (we will put the pointers to access the svcs in the given place specified by the client)
        service_installer_apply_one(
            &curr_svc[i],
            &cursor,
            &req_acrt,
            plan->pc_base
        );
    }

    svc_num_ptr = (seL4_Word *)((char *)monitor_svcdb_base + 0x1000 * plan->pc_id);
    svc_data_ptr = (unsigned char*)(svc_num_ptr + 1);

    *svc_num_ptr =
            req_acrt.num_req_notifications + \
            req_acrt.num_req_ppcs + \
            req_acrt.num_req_ioports + \
            req_acrt.num_req_mappings + \
        req_acrt.num_req_irqs;

    tsldr_main_monitor_encode_required_rights(svc_data_ptr, &req_acrt);
}
