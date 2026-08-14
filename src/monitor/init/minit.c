/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <libmicrokitco.h>
#include <assert.h>
#include <stdio.h>


dlg_header_t dlg;

svc_t svc;


static inline void
ca_monitor_init_storage(void)
{
    fs_cmpl_t completion;
    int err = fs_command_blocking(
        &completion,
        (fs_cmd_t){ .type = FS_CMD_INITIALISE }
    );

    if (err || completion.status != FS_STATUS_SUCCESS) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to mount\n");
        microkit_internal_crash(-1);
    }

    char svc_file[80];
    char dlg_file[80];

    snprintf(svc_file, sizeof(svc_file), "%s.svc", microkit_name);
    snprintf(dlg_file, sizeof(dlg_file), "%s.dlg", microkit_name);

    pico_vfs_readfile2buf((void *)0xaaaaa00000, svc_file, &err);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to load svc file\n");
        microkit_internal_crash(-1);
    }

    pico_vfs_readfile2buf((void *)0xaaaaa10000, dlg_file, &err);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to load dlg file\n");
        microkit_internal_crash(-1);
    }

    if (!dlg_parse((void *)0xaaaaa10000, &dlg)) {
        TSLDR_DBG_PRINT(PROGNAME "failed to parse dlg\n");
        while (1);
    }

    // for (uint32_t i = 0; i < dlg.delegator_count; i++) {
    //     const dlg_delegator_t *delegator = dlg.delegators[i];

    //     for (uint16_t j = 0; j < delegator->resource_count; j++) {
    //         const dlg_resource_t *resource = dlg_delegator_resource(delegator, j);
    //     }
    // }

    for (uint32_t i = 0; i < dlg.delegator_count; i++) {
        const dlg_delegator_t *delegator = dlg.delegators[i];

        TSLDR_DBG_PRINT("pd=%d cap=%d resources=%d\n",
                            delegator->pd_id,
                            delegator->delegation_cap,
                            delegator->resource_count);
    }

    if (!svc_parse((void *)0xaaaaa00000, &svc)) {
        TSLDR_DBG_PRINT(PROGNAME "failed to parse svc\n");
        while (1);
    }

    for (uint32_t i = 0; i < svc.service_count; i++) {
        const svc_service_t *service = svc.services[i];

        TSLDR_DBG_PRINT("pd=%d id=%d type=%d resources=%d path=%.*s\n",
                        service->pd_id,
                        service->service_id,
                        service->service_type,
                        service->resource_count,
                        (int)service->path_len,
                        svc_service_path(service));

        for (uint32_t j = 0; j < service->resource_count; j++) {
            const svc_resource_t *resource = svc_service_resource(service, j);
            TSLDR_DBG_PRINT("  kind=%d value=0x%x\n", resource->kind, resource->value);
        }
    }
}

static inline void
ca_monitor_init_cothread_spawn(const client_entry_t client_entry, void *arg, char err_msg[])
{
    if (microkit_cothread_spawn(client_entry, arg) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(err_msg);
        while(1);
    }
    microkit_cothread_yield();
}

static inline pc_monitor_Error
ca_monitor_init_validate_pc_count(uint32_t pc_count)
{
    if (pc_count > PC_CHILD_PER_MONITOR_MAX_NUM) {
        return mon_InvalidReqPCNum;
    }

    TSLDR_DBG_PRINT(
        PROGNAME
        "Number of available PCs recorded from svcdb: %d\n",
        pc_count
    );
    return mon_NoError;
}


static inline void
ca_monitor_init_get_pcnum(uint32_t delegator_cnt, ca_monitor_bootinfo_t *info)
{
    if (ca_monitor_init_validate_pc_count(delegator_cnt) != mon_NoError) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Invalid PC count: %d; maximum supported count is %d\n",
            delegator_cnt,
            (PC_CHILD_PER_MONITOR_MAX_NUM)
        );
        while(mon_InvalidReqPCNum);
    }
    info->num_pc = delegator_cnt;
    assert(info->num_pc <= PC_CHILD_PER_MONITOR_MAX_NUM);
}


static inline void
ca_monitor_init_protocon_states(uint64_t pc_num)
{
    for (uint64_t i = 0; i < pc_num; ++i) {
        protocon_states[i].pc_id = i;
        protocon_state_memzero_services(i);
        protocon_state_memzero_context(i);
        monitor_main_load_trustedlo(i);
        SET_PROTOCON_AS_AVAILABLE(i);
    }

    service_registry_create(&svc, protocon_states, pc_num);
}


void ca_monitor_init_states(void)
{
    void *binfo = microkit_cothread_my_arg();

    assert(binfo);
    ca_monitor_bootinfo_t *bootinfo =
                    (ca_monitor_bootinfo_t *)(binfo);

    while (!dlg.delegator_count) {
        microkit_cothread_yield();
    }    

    // 'dlg' is initiliased by now.
    ca_monitor_init_get_pcnum(dlg.delegator_count, bootinfo);

    /* init all protocon and states */
    ca_monitor_init_protocon_states(bootinfo->num_pc);

    // monitor_deploy_refresh_request();

    monitor_init_all_client_links(bootinfo->num_pc);
}

/* ----------------  Public Below  ------------------- */

void
ca_monitor_init_system(void *binfo)
{
    (void) ca_monitor_init_cothread_spawn(
        ca_monitor_init_storage,
        NULL,
        "failed to spawn thread for storage initialisation.\n"
    );

    (void) ca_monitor_init_cothread_spawn(
        ca_monitor_init_states,
        binfo,
        "failed to spawn thread for state initialisation.\n"
    );
}
