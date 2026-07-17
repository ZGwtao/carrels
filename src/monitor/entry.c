
#include <microkit.h>
#include <stdarg.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/util/printf.h>
#include <libtrustedlo.h>

#include <lions/fs/config.h>
#include <pico_vfs.h>
#include <carrels-monitor.h>
#include <libmicrokitco.h>
#include <pc_config.h>
#include <forwarder.h>
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

    // monitor_deploy_refresh_request();

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
