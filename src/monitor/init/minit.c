
#include <carrels-monitor.h>
#include <libmicrokitco.h>


static inline void
monitor_main_init_storage(void)
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
monitor_main_cothread_spawn(const client_entry_t client_entry, void *arg, char err_msg[])
{
    if (microkit_cothread_spawn(client_entry, arg) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(err_msg);
        while(1);
    }
    microkit_cothread_yield();
}

static inline void
monitor_main_init_protocon_states(uint32_t pc_num)
{
    for (uint32_t i = 0; i < pc_num; ++i) {
        protocon_states[i].pc_id = i;
        protocon_state_memzero_services(i);
        protocon_state_memzero_context(i);
        SET_PROTOCON_AS_AVAILABLE(i);
    }
    service_registry_create(&monitor_svc_db, protocon_states);
}

void monitor_main_init_system(void)
{
    /* init all protocon and states */
    monitor_main_init_protocon_states(PC_CHILD_PER_MONITOR_MAX_NUM);

    for (uint32_t i = 0; i < 4; ++i) {
        monitor_main_load_trustedlo(i);
    }

    (void) monitor_main_cothread_spawn(
        monitor_main_init_storage,
        NULL,
        "failed to spawn thread for storage initialisation.\n"
    );

    // monitor_deploy_refresh_request();

    monitor_init_all_client_links();
}
