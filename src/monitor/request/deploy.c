
#include <assert.h>
#include <carrels-monitor.h>
#include <libmicrokitco.h>


uint32_t req_pc_num = 0;
bool deploy_request_active = false;
monitor_deploy_request_t deploy_request;


static inline pc_monitor_Error
monitor_reset_deploy_request(seL4_Word num_req_pc)
{
    if (deploy_request_active) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Rejected deploy request: another deployment is still active\n"
        );
        return mon_FailToDeploy;
    }
    deploy_request.num_req_pc = (uint32_t)num_req_pc;
    req_pc_num = (uint32_t)num_req_pc;
    deploy_request_active = true;
    return mon_NoError;
}

static inline void
monitor_deploy_refresh_request(void)
{
    req_pc_num = 0;
    deploy_request.num_req_pc = 0;
    deploy_request_active = false;
}

static inline void
monitor_finish_deploy_request(void)
{
    monitor_deploy_refresh_request();
    monitor_main_notify_orchestrator();
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


static inline pc_monitor_Error
monitor_check_deploy_num(seL4_Word num_req_pc)
{
    if (num_req_pc < MIN_REQ_PC_NUM || num_req_pc > MAX_REQ_PC_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME "Invalid requested PC count: %d\n",
            num_req_pc
        );
        return mon_InvalidReqPCNum;
    }
    return mon_NoError;
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
monitor_call_deploy_second_half(void)
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

    err = service_manifest_header_parse(&payload_info,
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


static inline pc_monitor_Error
monitor_deploy_second_half(void)
{
    if (microkit_cothread_spawn(
            monitor_call_deploy_second_half,
            &deploy_request
        ) == LIBMICROKITCO_NULL_HANDLE)
    {
        TSLDR_DBG_PRINT(
            PROGNAME
            "cannot initialise monitor cothread for monitor call.\n"
        );
        monitor_finish_deploy_request();
        return mon_FailToInitCoroutine;
    }
    return mon_NoError;
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


static inline void
protocon_start(deploy_plan_t *plan)
{
    mktxlo_prepare_txlo_info(
        (txlo_monitor_t *)microkit_trusted_loading_info,
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
        sizeof(trustedlo_ctxt_t)
    );

    mktxlo_privilege_template_pd(plan->pc_id);

    SET_PROTOCON_AS_INSTANTIATED(plan->pc_id)

    microkit_pd_restart(plan->pc_id, plan->pc_entry);
    TSLDR_DBG_PRINT(
        PROGNAME
        "Started child PD at entrypoint address: %x\n",
        plan->pc_entry
    );
}


seL4_MessageInfo_t
monitor_call_deploy_first_half(seL4_Word num_req_pc)
{
    pc_monitor_Error err = mon_NoError;

    err = monitor_check_deploy_num(num_req_pc);
    if (err != mon_NoError) {
        goto fh_exit;
    }

    err = monitor_reset_deploy_request(num_req_pc);
    if (err != mon_NoError) {
        goto fh_exit;
    }

    err = monitor_deploy_second_half();
    if (err != mon_NoError) {
        goto fh_exit;
    }

    /* let the filesystem coroutine execute */
    microkit_cothread_yield();
fh_exit:
    return microkit_msginfo_new(err, 0);
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

    service_installer_apply(&plan);

    protocon_start(&plan);
    return err;
}
