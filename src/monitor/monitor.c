
#include <microkit.h>
#include <stdarg.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/util/printf.h>
#include <libtrustedlo.h>

#include <lions/fs/config.h>
#include <pico_vfs.h>
#include <ossvc.h>
#include <libmicrokitco.h>
#include <pc_config.h>
#include <protocon.h>
#include <monitor.h>
#include <fault.h>
#include <deploy.h>
#include <query.h>
#include <resume.h>
#include <forwarder.h>
#include <payload.h>
#include <pd_io_queue.h>
#include <tsldr_vm_layout.h>
#include <monitor_vm_layout.h>



__attribute__((__section__(".serial_client_config")))
serial_client_config_t serial_config;
__attribute__((__section__(".fs_client_config")))
fs_client_config_t fs_config;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

// these are the craziest thing for microkit cothreads
co_control_t co_controller_mem;
static char monitor_costack1[0x10000];
static char monitor_costack2[0x10000];
static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }


pc_state_t protocon_states[PC_CHILD_PER_MONITOR_MAX_NUM];

#define ORC_MONITOR_REGION_SIZE (0x800000)
#define ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE (0x7000000)
uintptr_t __carrels_payload_start = (uintptr_t)(ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE);


seL4_Word pd_io_acl_rule = 0;


__attribute__((__section__(".monitor_svc_db")))
monitor_svcdb_t monitor_svc_db;



void monitor_main_init_storage(void)
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
monitor_main_load_trustedlo(uint32_t cid)
{
    uintptr_t protocon_base =
            monitor_vm_region_base(&monitor_vm_layout.loader_program, cid);
    uintptr_t trampoline_base =
            monitor_vm_region_base(&monitor_vm_layout.trampoline_image, cid);
    uintptr_t payload_base =
            monitor_vm_region_base(&monitor_vm_layout.container_image, cid);

    size_t protocon_size = monitor_protocon_capacity();
    size_t trampoline_size = monitor_trampoline_capacity();

    tsldr_miscutil_memset((void *)protocon_base, 0, protocon_size);
    tsldr_miscutil_memset((void *)trampoline_base, 0, trampoline_size);

    tsldr_miscutil_load_elf(
        (void*)protocon_base,
        (const Elf64_Ehdr *)(__carrels_protocon_start)
    );
    tsldr_miscutil_memcpy(
        (void*)trampoline_base,
        (const char *)(__carrels_trampoline_start),
        trampoline_size
    );

    /* clean up client payload region entirely. */
    tsldr_miscutil_memset((void *)payload_base, 0, ORC_MONITOR_REGION_SIZE);
}


seL4_MessageInfo_t monitor_call_restore_protocon(microkit_channel ch)
{
    int cid = monitor_main_get_cid_from_channel(ch);
    if (cid == (INVALID_PC_ID)) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id to restore given with ch: %d\n", ch);
    } else {
        SET_PROTOCON_AS_AVAILABLE(cid)
        monitor_main_load_trustedlo(cid);
    }
    monitor_main_notify_orchestrator();
    return microkit_msginfo_new(mon_NoError, 0);
}

seL4_MessageInfo_t monitor_call_stop_and_restore_protocon(microkit_channel ch)
{
    int target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for stop and restore\n");
        return microkit_msginfo_new(mon_InvalidPCId, 0);
    }
    microkit_pd_stop(target_pd_id);
    return monitor_call_restore_protocon(target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL);
}

seL4_MessageInfo_t monitor_call_hang_protocon(microkit_channel ch)
{
    int target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for hang\n");
        return microkit_msginfo_new(mon_InvalidPCId, 0);
    }
    int cid_to_check = target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL;
    int cid = monitor_main_get_cid_from_channel(cid_to_check);
    if (cid == (INVALID_PC_ID)) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id to restore given with ch: %d\n", cid_to_check);
    } else {
        if (!protocon_state_check_lifecycle_state(cid, PROTOCON_ACTIVE)) {
            TSLDR_DBG_PRINT(PROGNAME "PD to hang must be active first!\n");
        } else {
            SET_PROTOCON_AS_HANG(cid)
            microkit_pd_stop(target_pd_id);
        }
    }
    monitor_main_notify_orchestrator();
    return microkit_msginfo_new(mon_NoError, 0);
}


seL4_MessageInfo_t monitor_call_backup_protocon_loading_context(microkit_channel ch)
{
    int cid = monitor_main_get_cid_from_channel(ch);

    tsldr_context_t *context = \
        (tsldr_context_t *)monitor_vm_region_base(&monitor_vm_layout.loader_context, cid);

    tsldr_miscutil_memcpy(protocon_state_retrieve_context(cid), context, sizeof(tsldr_context_t));

    return microkit_msginfo_new(mon_NoError, 0);
}

static inline seL4_MessageInfo_t
monitor_main_handle_pccall(microkit_channel ch)
{
    /* get the first word of the message */
    seL4_Word call_id = microkit_mr_get(0);
    seL4_MessageInfo_t ret = microkit_msginfo_new(0, 0);

    /* call for the container monitor */
    switch (call_id) {
    case PC_MONITOR_CALL_DEPLOY:
        TSLDR_DBG_PRINT(PROGNAME "Deploy an application to a dynamic PD\n");
        seL4_Word num_req_pc = microkit_mr_get(1);
        ret = monitor_call_deploy_first_half(num_req_pc);
        break;
    case PC_MONITOR_CALL_FLIP_ACL_RULE:
        pd_io_acl_rule = !pd_io_acl_rule;
        break;
    case PC_MONITOR_CALL_BACKUP_CONTEXT:
        TSLDR_DBG_PRINT(PROGNAME "Backing up trusted loading context for dynamic PD with ID: %d\n", monitor_main_get_cid_from_channel(ch));
        ret = monitor_call_backup_protocon_loading_context(ch);
        break;
    case PC_MONITOR_CALL_TERMINATE:
        TSLDR_DBG_PRINT(PROGNAME "Exit and uninstantiate a dynamic PD\n");
        ret = monitor_call_stop_and_restore_protocon(ch - PC_MONITOR_PROTOCON_BASE_CHANNEL);
        break;
    case PC_MONITOR_CALL_HANG: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        TSLDR_DBG_PRINT(PROGNAME "Hang dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_hang_protocon(target_pd_id);
        break;
    }
    case PC_MONITOR_CALL_RESUME: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        TSLDR_DBG_PRINT(PROGNAME "Resume dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_resume_protocon(target_pd_id);
        break;
    }
    case PC_MONITOR_CALL_LIST_PROTOCONS:
        TSLDR_DBG_PRINT(PROGNAME "List states of dynamic PDs\n");
        ret = monitor_call_list_protocons();
        break;
    case PC_MONITOR_CALL_QUERY_PROTOCONS:
        TSLDR_DBG_PRINT(PROGNAME "Query states of dynamic PDs\n");
        ret = monitor_call_query_protocons(ch);
        break;
    case PC_MONITOR_CALL_TERMINATE_EXT: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        if (ch >= 24) {
            target_pd_id = monitor_main_get_cid_from_channel(ch);
        }
        TSLDR_DBG_PRINT(PROGNAME "Terminate dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_stop_and_restore_protocon(target_pd_id);
        break;
    }
    default:
        /* do nothing for now */
        TSLDR_DBG_PRINT(PROGNAME "Undefined container monitor call: %d\n", call_id);
        break;
    }

    return ret;
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

static inline void
monitor_main_init_system(void)
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

    monitor_deploy_refresh_request();

    monitor_init_all_client_links();
}


void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle,
                          serial_config.rx.queue.vaddr,
                          serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle,
                      serial_config.tx.queue.vaddr,
                      serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    assert(fs_config_check_magic(&fs_config));

    fs_set_blocking_wait(blocking_wait);

    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    stack_ptrs_arg_array_t costacks = { (uintptr_t) monitor_costack1, (uintptr_t) monitor_costack2 };
    microkit_cothread_init(&co_controller_mem, 0x10000, costacks);

    monitor_main_init_system();
}

seL4_MessageInfo_t
protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return monitor_main_handle_pccall(ch);
}

 void notified(microkit_channel ch)
 {
    if (ch >= PD_IO_MONITOR_NOTIFY_BASE &&
        ch < PD_IO_MONITOR_NOTIFY_BASE + PD_IO_CLIENT_COUNT) {
        uint32_t cid = ch - PD_IO_MONITOR_NOTIFY_BASE;
        monitor_handle_client_payload(cid);
        return;
    }

    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(ch);
}


seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    monitor_main_handle_fault(child, msginfo);

    // Stop the thread explicitly; no need to reply to the fault
    return seL4_False;
}
