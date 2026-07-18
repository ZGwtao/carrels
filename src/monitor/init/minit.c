
#include <carrels-monitor.h>
#include <libmicrokitco.h>
#include <assert.h>


static inline void
ca_monitor_init_storage(void)
{
    TSLDR_DBG_PRINT(PROGNAME "(fs mount) start fs initialisation\n");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to mount\n");
        microkit_internal_crash(-1);
    }
    TSLDR_DBG_PRINT(PROGNAME "(fs mount) finished fs initialisation\n");
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
ca_monitor_init_validate_pc_count(size_t pc_count)
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
ca_monitor_init_get_pcnum(const monitor_svcdb_t *svcdb_list, ca_monitor_bootinfo_t *info)
{
    uint64_t n = svcdb_list->len;
    if (ca_monitor_init_validate_pc_count(n) != mon_NoError) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Invalid PC count: %d; maximum supported count is %d\n",
            n,
            (PC_CHILD_PER_MONITOR_MAX_NUM)
        );
        while(mon_InvalidReqPCNum);
    }
    info->num_pc = n;
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

    service_registry_create(
        &monitor_svc_db,
        protocon_states,
        pc_num
    );
}


/* ----------------  Public Below  ------------------- */

void
ca_monitor_init_system(void *binfo)
{
    assert(binfo);
    ca_monitor_bootinfo_t *bootinfo =
                    (ca_monitor_bootinfo_t *)(binfo);

    ca_monitor_init_get_pcnum(&monitor_svc_db, bootinfo);

    /* init all protocon and states */
    ca_monitor_init_protocon_states(bootinfo->num_pc);

    (void) ca_monitor_init_cothread_spawn(
        ca_monitor_init_storage,
        NULL,
        "failed to spawn thread for storage initialisation.\n"
    );

    // monitor_deploy_refresh_request();

    monitor_init_all_client_links(bootinfo->num_pc);
}
