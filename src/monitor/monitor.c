
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
#include <forwarder.h>
#include <payload.h>
#include <pd_io_queue.h>
#include <tsldr_vm_layout.h>
#include <monitor_vm_layout.h>

// these memory regions are shared memory between
//    -> the monitor (container monitor)
//    -> the dynamic pds (protocon - proto containers)
// the size of these memory regions are:
//    -> PC_MONITOR_REGION_SIZE (for one dynamic pd)
//
#define PC_MONITOR_REGION_SIZE MONITOR_VM_LOADER_PROGRAM_SIZE
#define PC_MONITOR_REGION_PROTOCON_ELF_BASE MONITOR_VM_LOADER_PROGRAM_BASE
#define PC_MONITOR_REGION_TRAMPOLINE_ELF_BASE MONITOR_VM_TRAMPOLINE_IMAGE_BASE
#define PC_MONITOR_REGION_CLIENT_PAYLOAD_BASE MONITOR_VM_CONTAINER_IMAGE_BASE

// each elf file is of the same upper size limit
#define ORC_MONITOR_REGION_SIZE (0x800000)
// elf files from orchestrator as external files...
// shared memory with the orchestrator PD
#define ORC_MONITOR_REGION_PROTOCON_ELF_BASE (0x6000000)
#define ORC_MONITOR_REGION_TRAMPOLINE_ELF_BASE (0x6800000)
#define ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE (0x7000000)

uintptr_t __carrels_payload_start = (uintptr_t)(ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE);

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


#define SMALL_PAGE_SIZE     (0x1000)

// this is the base address of the trusted loader context region for each dynamic PD (protocon)
// this describes the information of all requested low-level access rights of a dynamic PD, which is the SUBSET of trusted loading metadata
#define TSLDR_CONTEXT_BASE  MONITOR_VM_LOADER_CONTEXT_BASE
#define TSLDR_CONTEXT_SIZE  MONITOR_VM_LOADER_CONTEXT_SIZE
// this is the base address of the trusted loader metadata region for each dynamic PD (protocon)
// the monitor PD will prepare the metadata for each dynamic PD in this region, and the dynamic PD will read the metadata from this region when it is loading
// this describes the information of all low-level access rights of a dynamic PD, which will be used by the trusted loader to do the actual loading work
#define TSLDR_METADATA_BASE MONITOR_VM_LOADER_METADATA_BASE
#define TSLDR_METADATA_SIZE MONITOR_VM_LOADER_METADATA_SIZE

seL4_Word pd_io_acl_rule = 0;

// base of all shared os services metadata regions
// the region is for all dynamic PDs, each dynamic PD has a piece between the monitor PD and the dynamic PD itself
// we use it to store the information of OS services requested by the client program
// we will encode the low-level access rights information of OS services requested
// and put the serialised, encoded info into this region.
// the dynamic PD can then read the OS svc information at high-level, while initialise the trusted loader
// with the low-level information provided here...
uintptr_t msvcdb_base = MONITOR_VM_OSSVC_METADATA_BASE;

__attribute__((__section__(".monitor_svc_db"))) monitor_svcdb_t monitor_svc_db;

#define MIN_REQ_PC_NUM 1U
#define MAX_REQ_PC_NUM 4U

typedef struct monitor_deploy_request {
    uint32_t num_req_pc;
} monitor_deploy_request_t;

/*
 * A value of zero means that there is no active deployment request.
 * The request-specific count is copied into a local variable by the
 * deployment cothread before it performs any work.
 */
static uint32_t req_pc_num = 0;
static bool deploy_request_active = false;
static monitor_deploy_request_t deploy_request;

static void monitor_finish_deploy_request(void)
{
    req_pc_num = 0;
    deploy_request.num_req_pc = 0;
    deploy_request_active = false;

    monitor_main_notify_orchestrator();
}

#define SET_PROTOCON_AS_INSTANTIATED(C) \
    do { protocon_state_set_lifecycle_state(C, PROTOCON_ACTIVE); } while (0);

#define SET_PROTOCON_AS_HANG(C) \
    do { protocon_state_set_lifecycle_state(C, PROTOCON_HANG); } while (0);

#define SET_PROTOCON_AS_AVAILABLE(C) \
    do { protocon_state_set_lifecycle_state(C, PROTOCON_PASSIVE); } while (0);


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


static inline
pc_monitor_Error protocon_deploy_plan_check(deploy_plan_t *plan)
{
    if (plan->pc_id >= PC_CHILD_PER_MONITOR_MAX_NUM || plan->pc_id < 0) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Failed to find suitable container for payload\n"
        );
        return mon_NoAvailPc;
    }
    TSLDR_DBG_PRINT(PROGNAME "cid available: %d\n", plan->pc_id);
    return mon_NoError;
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


static inline void
protocon_load_payload(uintptr_t dest, uintptr_t src, uint64_t size)
{
    tsldr_miscutil_memcpy(
        (void *)dest,
        (const void *)src,
        size
    );
    TSLDR_DBG_PRINT(
        PROGNAME "src: %x, dest: %x, size: %d\n",
        src,
        dest,
        size
    );
}

static inline void
protocon_pre_instantiate(deploy_plan_t *plan, const payload_info_t *payload)
{
    uintptr_t dest =
        monitor_vm_region_base(
            &monitor_vm_layout.container_image,
            plan->pc_id
        );
    plan->pc_base = dest;
    assert(plan->pc_base != 0x0);

    plan->pc_entry =
        (Elf64_Addr)(tsldr_vm_layout.loader_program.base);
    assert(plan->pc_entry == ((Elf64_Ehdr *)(__carrels_protocon_start))->e_entry);

    protocon_load_payload(
        (uintptr_t)(plan->pc_base),
        (uintptr_t)(payload->header_payload),
        (uint64_t)(payload->elf_payload_size)
    );
}


void protocon_start(deploy_plan_t *plan)
{
    tsldr_main_monitor_init_mdinfo(
        (tsldr_mdinfodb_t *)microkit_trusted_loading_info,
        plan->pc_id,
        (void *)monitor_vm_region_base(
            &monitor_vm_layout.loader_metadata,
            plan->pc_id
        )
    );

    tsldr_miscutil_memcpy(
        (char *)monitor_vm_region_base(
            &monitor_vm_layout.loader_context,
            plan->pc_id
        ),
        protocon_state_retrieve_context(plan->pc_id),
        sizeof(tsldr_context_t)
    );

    tsldr_main_monitor_privilege_pd(plan->pc_id);

    SET_PROTOCON_AS_INSTANTIATED(plan->pc_id)

    microkit_pd_restart(plan->pc_id, plan->pc_entry);
    TSLDR_DBG_PRINT(
        PROGNAME
        "Started child PD at entrypoint address: %x\n",
        plan->pc_entry
    );
}


pc_monitor_Error protocon_deploy(payload_info_t *info)
{
    deploy_plan_t plan = { 0 };
    protocon_svc_req_t req = { 0 };
    pc_monitor_Error err = mon_NoError;

    (void) service_manifest_parse(info, &req);

    (void) service_planner_select_protocon(
        &req,
        &plan,
        protocon_states
    );

    err = protocon_deploy_plan_check(&plan);
    if (err != mon_NoError) {
        return err;
    }

    protocon_pre_instantiate(&plan, info);

    service_installer_apply(
        &plan,
        msvcdb_base,
        &monitor_svc_db.list[plan.pc_id]
    );

    protocon_start(&plan);
    return err;
}


void monitor_call_deploy_protocon_second_half(void)
{
    seL4_Error err = seL4_NoError;
    monitor_deploy_request_t *request = microkit_cothread_my_arg();
    payload_info_t payload_info = { 0 };
    uint32_t num_req_pc = request->num_req_pc;

    TSLDR_DBG_PRINT(PROGNAME "entry of monitor_call_deploy_protocon_second_half\n");

    if (num_req_pc < MIN_REQ_PC_NUM || num_req_pc > MAX_REQ_PC_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME "Invalid active deployment request count: %u\n",
            num_req_pc
        );
        monitor_finish_deploy_request();
        return;
    }

    err = payload_info_parse(&payload_info,
                             (__carrels_payload_start));
    if (err != seL4_NoError) {
        monitor_finish_deploy_request();
        return;
    }

    for (uint32_t i = 0; i < num_req_pc; ++i)
    {
        if (protocon_deploy(&payload_info) != mon_NoError) {
            TSLDR_DBG_PRINT(
                PROGNAME
                "Failed to deploy container\n"
            );
            break;
        }
    }

    monitor_finish_deploy_request();
}

seL4_MessageInfo_t monitor_call_deploy_protocon_first_half(seL4_Word num_req_pc)
{
    if (num_req_pc < MIN_REQ_PC_NUM || num_req_pc > MAX_REQ_PC_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME "Invalid requested PC count: %lu\n",
            (unsigned long)num_req_pc
        );
        return microkit_msginfo_new(mon_InvalidReqPCNum, 0);
    }

    /*
     * The orchestrator ELF buffers and deployment context are shared.
     * Only one deployment request may be active at a time.
     */
    if (deploy_request_active) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Rejected deploy request: another deployment is still active\n"
        );
        return microkit_msginfo_new(-1, 0);
    }

    TSLDR_DBG_PRINT(
        PROGNAME "entry of monitor_call_deploy_protocon_first_half\n"
    );

    seL4_Word err;
    tsldr_main_check_elf_integrity(
        ORC_MONITOR_REGION_PROTOCON_ELF_BASE,
        &err
    );
    if (err) {
        TSLDR_DBG_PRINT(
            PROGNAME "Integrity check failed for protocon elf\n"
        );
        monitor_main_notify_orchestrator();
        return microkit_msginfo_new(err, 0);
    }

    deploy_request.num_req_pc = (uint32_t)num_req_pc;
    req_pc_num = (uint32_t)num_req_pc;
    deploy_request_active = true;

    TSLDR_DBG_PRINT(
        PROGNAME "Integrity check passed for protocon elf\n"
    );

    if (microkit_cothread_spawn(
            monitor_call_deploy_protocon_second_half,
            &deploy_request
        ) == LIBMICROKITCO_NULL_HANDLE)
    {
        TSLDR_DBG_PRINT(
            PROGNAME
            "cannot initialise monitor cothread for monitor call.\n"
        );
        monitor_finish_deploy_request();
        return microkit_msginfo_new(mon_FailToInitCoroutine, 0);
    }

    microkit_cothread_yield();
    return microkit_msginfo_new(mon_NoError, 0);
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

seL4_MessageInfo_t monitor_call_resume_protocon(microkit_channel ch)
{
    int target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for resume\n");
        return microkit_msginfo_new(mon_InvalidPCId, 0);
    }
    int cid_to_check = target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL;
    int cid = monitor_main_get_cid_from_channel(cid_to_check);
    if (cid == (INVALID_PC_ID)) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id to resume given with ch: %d\n", cid_to_check);
    } else {
        if (!protocon_state_check_lifecycle_state(cid, PROTOCON_HANG)) {
            TSLDR_DBG_PRINT(PROGNAME "Invalid PD state to resume!\n");
        } else {
            microkit_pd_resume(target_pd_id);
            SET_PROTOCON_AS_INSTANTIATED(cid)
        }
    }
    monitor_main_notify_orchestrator();
    return microkit_msginfo_new(mon_NoError, 0);
}

static inline void monitor_main_list_protocon_states(int num_protocons)
{
    if (num_protocons > PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid number of protocons to list: %d\n", num_protocons);
        return;
    }
    for (int i = 0; i < num_protocons; ++i) {
        sddf_printf("[*] dynamic-PD [id=%d] has state: ", i);
        const protocon_lifecycle_state_t state = 
                protocon_state_get_lifecycle_state(i);
        switch (state) {
            case PROTOCON_ACTIVE:
                sddf_printf("in-use");
                break;
            case PROTOCON_PASSIVE:
                sddf_printf("avail");
                break;
            case PROTOCON_HANG:
                sddf_printf("hang");
                break;
            default:
                sddf_printf("unknown: %d", state);
        };
        sddf_printf("\n");
    }
}

seL4_MessageInfo_t monitor_call_list_protocons()
{
    // FIXME: should not hardcode the number of pc to list
    monitor_main_list_protocon_states(4);
    return microkit_msginfo_new(mon_NoError, 0);
}

seL4_MessageInfo_t monitor_call_query_protocons(microkit_channel ch)
{
    // FIXME: should not hardcode the number of pc to list
    monitor_main_list_protocon_states(4);

    seL4_Word self_id = monitor_main_get_cid_from_channel(ch);
    seL4_Word bitmap = 0;
    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if ((protocon_state_check_lifecycle_state(i, PROTOCON_ACTIVE) ||
             protocon_state_check_lifecycle_state(i, PROTOCON_HANG)) && i != self_id) {
            bitmap |= (1ULL << i);
        }
    }
    seL4_MessageInfo_t ret = microkit_msginfo_new(mon_NoError, 2);
    microkit_mr_set(0, bitmap);
    microkit_mr_set(1, monitor_main_get_cid_from_channel(ch));
    if (bitmap == 0) {
        sddf_printf("No dynamic PDs are currently available for communication\n");
    }
    return ret;
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
        ret = monitor_call_deploy_protocon_first_half(num_req_pc);
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

    req_pc_num = 0;
    deploy_request.num_req_pc = 0;
    deploy_request_active = false;

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
