/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <carrels-monitor.h>
#include <libmicrokitco.h>

uint32_t req_pc_num = 0;
bool deploy_request_active = false;
monitor_deploy_request_t deploy_request;
monitor_deploy_result_t deploy_result;

static pc_monitor_error protocon_deploy_to_protocon(payload_info_t *info, uint32_t pc_id);
static pc_monitor_error protocon_deploy_execute(payload_info_t *info, deploy_plan_t *plan);
static inline pc_monitor_error protocon_deploy_plan_check(deploy_plan_t *plan);

static void monitor_deploy_result_begin(uint32_t requested_count)
{
    deploy_result = (monitor_deploy_result_t){
        .response.requested_count = requested_count,
        .error = MON_DEPLOY_IN_PROGRESS,
        .ready = false,
    };
}

static void monitor_deploy_result_record(uint32_t pc_id)
{
    deploy_result.response.deployed_count++;
    deploy_result.response.protocon_mask |= (uint32_t)1U << pc_id;
}

static void monitor_deploy_result_complete(pc_monitor_error error)
{
    deploy_result.error = error;
    deploy_result.ready = true;
}

static void release_batch_reservations(deploy_plan_t plans[], uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        SET_PROTOCON_AS_AVAILABLE(plans[i].pc_id)
    }
}

static pc_monitor_error plan_batch_deployment(const protocon_svc_req_t *request,
                                               deploy_plan_t plans[],
                                               uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        service_planner_select_protocon(request, &plans[i], protocon_states);
        pc_monitor_error error = protocon_deploy_plan_check(&plans[i]);
        if (error != MON_NO_ERROR) {
            release_batch_reservations(plans, i);
            return error;
        }

        /* Reserve every target before starting any payload. This prevents a
         * fast-exiting instance from being selected twice within one batch. */
        protocon_state_set_lifecycle_state(plans[i].pc_id, PROTOCON_RESERVED);
    }

    return MON_NO_ERROR;
}

static pc_monitor_error execute_batch_deployment(payload_info_t *payload,
                                                  deploy_plan_t plans[],
                                                  uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pc_id = plans[i].pc_id;
        uint64_t started_at = monitor_profile_counter();

        monitor_profile_begin_deploy(pc_id, started_at);
        monitor_profile_record(pc_id, PC_PROFILE_DEPLOY_PARSE_PLAN, started_at);

        pc_monitor_error error = protocon_deploy_execute(payload, &plans[i]);
        if (error != MON_NO_ERROR) {
            SET_PROTOCON_AS_AVAILABLE(pc_id)
            release_batch_reservations(&plans[i + 1], count - i - 1);
            return error;
        }

        monitor_deploy_result_record(pc_id);
    }

    return MON_NO_ERROR;
}

static inline pc_monitor_error monitor_reset_deploy_request(seL4_Word num_req_pc)
{
    if (deploy_request_active) {
        TSLDR_DBG_PRINT(PROGNAME "Rejected deploy request: another deployment is still active\n");
        return MON_FAIL_TO_DEPLOY;
    }
    deploy_request.num_req_pc = (uint32_t)num_req_pc;
    deploy_request.target_pc = 0;
    deploy_request.requested_peer_mask = 0;
    deploy_request.targeted = false;
    req_pc_num = (uint32_t)num_req_pc;
    deploy_request_active = true;
    monitor_deploy_result_begin((uint32_t)num_req_pc);
    return MON_NO_ERROR;
}

static inline void monitor_deploy_refresh_request(void)
{
    req_pc_num = 0;
    deploy_request.num_req_pc = 0;
    deploy_request.target_pc = 0;
    deploy_request.requested_peer_mask = 0;
    deploy_request.targeted = false;
    deploy_request_active = false;
}

static inline void monitor_finish_deploy_request(void)
{
    monitor_deploy_refresh_request();
    monitor_main_notify_orchestrator();
}

static inline uint64_t mktsymb_bundle_size(const mktsymb_header_t *header)
{
    const uint8_t *p = header->symbols;

    for (uint32_t i = 0; i < header->symbol_cnt; ++i) {
        mktsymb_symbol_t symbol;
        p = mktsymb_read_symbol(p, &symbol);
    }

    return (uint64_t)(p - header->base);
}

static inline void
protocon_load_payload(uint32_t pc_id, uintptr_t dest, uintptr_t src, uint64_t elf_size)
{
    const mktsymb_header_t *sym = &protocon_states[pc_id].sym_header;
    size_t previous_image_size = protocon_states[pc_id].loaded_image_size;
    uint64_t sym_size = mktsymb_bundle_size(sym);
    uint64_t sym_offset = sizeof(protocon_image_header_t);
    uint64_t elf_offset = sym_offset + sym_size;
    size_t image_size = elf_offset + elf_size;

    protocon_image_header_t *header = (protocon_image_header_t *)dest;

    header->magic = PROTOCON_IMAGE_MAGIC;
    header->reserved = 0;
    header->mktsymb_offset = sym_offset;
    header->mktsymb_size = sym_size;
    header->elf_offset = elf_offset;
    header->elf_size = elf_size;

    memcpy((void *)(dest + sym_offset), sym->base, sym_size);
    memcpy((void *)(dest + elf_offset), (const void *)src, elf_size);

    /* The new image overwrites its entire range. If the previous tenant used
     * a larger image, clear only the tail that is not overwritten before the
     * Protocon is restarted. This preserves isolation without performing a
     * redundant full clear during reclaim. */
    if (previous_image_size > image_size) {
        memset((void *)(dest + image_size), 0, previous_image_size - image_size);
    }
    protocon_states[pc_id].loaded_image_size = image_size;

    TSLDR_DBG_PRINT(PROGNAME "pc=%d image=%x sym_off=%x sym_size=%d elf_off=%x elf_size=%d\n",
                    pc_id,
                    dest,
                    sym_offset,
                    sym_size,
                    elf_offset,
                    elf_size);
}

static inline pc_monitor_error monitor_check_deploy_num(seL4_Word num_req_pc)
{
    if (num_req_pc < 1 || num_req_pc > ca_bootinfo.num_pc) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid requested PC count: %d\n", num_req_pc);
        return MON_INVALID_REQ_PC_NUM;
    }
    return MON_NO_ERROR;
}

static inline pc_monitor_error protocon_deploy_plan_check(deploy_plan_t *plan)
{
    if (plan->pc_id >= PC_CHILD_PER_MONITOR_MAX_NUM || plan->pc_id < 0) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to find suitable container for payload\n");
        return MON_NO_AVAIL_PC;
    }
    TSLDR_DBG_PRINT(PROGNAME "cid available: %d\n", plan->pc_id);
    return MON_NO_ERROR;
}

static inline void monitor_call_deploy_second_half(void)
{
    seL4_Error err;
    monitor_deploy_request_t *request = microkit_cothread_my_arg();
    payload_info_t payload_info = {0};
    uint32_t num_req_pc = request->num_req_pc;

    TSLDR_DBG_PRINT(PROGNAME "entry of monitor_call_deploy_protocon_second_half\n");

    if (!request->targeted && monitor_check_deploy_num(num_req_pc) != MON_NO_ERROR) {
        monitor_deploy_result_complete(MON_INVALID_REQ_PC_NUM);
        monitor_finish_deploy_request();
        return;
    }

    err = service_manifest_header_parse(&payload_info,
                                        __carrels_payload_start,
                                        __carrels_payload_capacity);
    if (err != seL4_NoError) {
        monitor_deploy_result_complete(MON_FAIL_TO_DEPLOY);
        monitor_finish_deploy_request();
        return;
    }

    if (request->targeted) {
        pc_monitor_error error;

        error = protocon_deploy_to_protocon(&payload_info, request->target_pc);
        if (error == MON_NO_ERROR) {
            monitor_deploy_result_record(request->target_pc);
            error = monitor_acl_apply_deployment(request->target_pc,
                                                 request->requested_peer_mask,
                                                 &deploy_result.response.connected_peer_mask,
                                                 &deploy_result.response.rejected_peer_mask,
                                                 &deploy_result.response.unavailable_peer_mask);
        }
        monitor_deploy_result_complete(error);
    } else {
        deploy_plan_t plans[PC_CHILD_PER_MONITOR_MAX_NUM] = {0};
        protocon_svc_req_t service_request = {0};
        service_manifest_parse(&payload_info, &service_request);
        pc_monitor_error error = plan_batch_deployment(&service_request, plans, num_req_pc);

        if (error == MON_NO_ERROR) {
            error = execute_batch_deployment(&payload_info, plans, num_req_pc);
        }
        monitor_deploy_result_complete(error);
    }

    monitor_finish_deploy_request();
}

static inline pc_monitor_error monitor_deploy_second_half(void)
{
    if (microkit_cothread_spawn(monitor_call_deploy_second_half, &deploy_request) ==
        LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(PROGNAME "cannot initialise monitor cothread for monitor call.\n");
        monitor_finish_deploy_request();
        return MON_FAIL_TO_INIT_COROUTINE;
    }
    return MON_NO_ERROR;
}

static inline void protocon_pre_instantiate(deploy_plan_t *plan, const payload_info_t *payload)
{
    uintptr_t dest = monitor_vm_region_base(&monitor_vm_layout.container_image, plan->pc_id);
    plan->pc_base = dest;
    assert(plan->pc_base != 0x0);

    plan->pc_entry = (Elf64_Addr)(tsldr_vm_layout.loader_program.base);
    assert(plan->pc_entry == ((Elf64_Ehdr *)(__carrels_protocon_start))->e_entry);

    protocon_load_payload(plan->pc_id,
                          (uintptr_t)(plan->pc_base),
                          (uintptr_t)(payload->header_payload),
                          (uint64_t)(payload->elf_payload_size));
}

static inline void protocon_init_txlo_info(const deploy_plan_t *plan)
{
    const dlg_delegator_t *src = dlg_find_delegator(&dlg, plan->pc_id);
    TSLDR_ASSERT(src != NULL);
    TSLDR_ASSERT(src->record_size <= 4096);

    void *dest = (void *)monitor_vm_region_base(&monitor_vm_layout.loader_metadata, plan->pc_id);
    memcpy(dest, src, src->record_size);
}

static inline void protocon_init_txlo_context(const deploy_plan_t *plan)
{
    /* todo: make sure by default the context is memzero-ed. */
    trustedlo_ctxt_t *ctxt =
        (trustedlo_ctxt_t *)monitor_vm_region_base(&monitor_vm_layout.loader_context, plan->pc_id);
    memcpy(ctxt, protocon_state_retrieve_context(plan->pc_id), sizeof(trustedlo_ctxt_t));
    /* make sure txlo context initialises for only one time. */
    if (ctxt->txlo_monitor_init_field.switch_count >= TXLO_CTXT_MAX_SWITCH_CNT) {
        ctxt->txlo_monitor_init_field.switch_count = 1;
    }
    ctxt->txlo_monitor_init_field.child_id = plan->pc_id;
    ctxt->txlo_monitor_init_field.channel = (10 + 64 + 15);
    ctxt->txlo_monitor_init_field.call_id = PC_MONITOR_CALL_BACKUP_CONTEXT;
}

static inline void protocon_restart_loader(uint32_t pc_id, seL4_Word entry_point)
{
    seL4_UserContext ctxt = {0};

#if defined(CONFIG_ARCH_X86_64)
    ctxt.rip = entry_point;
    ctxt.rsp = TSLDR_VM_MICROKIT_X86_STACK_END;
#elif defined(CONFIG_ARCH_AARCH64)
    ctxt.pc = entry_point;
    ctxt.sp = TSLDR_VM_MICROKIT_AARCH64_STACK_END;
#else
#error "Unsupported architecture"
#endif

    seL4_Error err = seL4_TCB_WriteRegisters(BASE_TCB_CAP + pc_id,
                                              seL4_True,
                                              0,
                                              2,
                                              &ctxt);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to restart loader for child %u: %d\n", pc_id, err);
        microkit_internal_crash(err);
    }
}

static inline void protocon_start(deploy_plan_t *plan)
{
    protocon_init_txlo_info(plan);
    protocon_init_txlo_context(plan);

    mktxlo_privilege_template_pd(plan->pc_id);

    SET_PROTOCON_AS_INSTANTIATED(plan->pc_id)

    protocon_restart_loader(plan->pc_id, plan->pc_entry);
    TSLDR_DBG_PRINT(PROGNAME "Started child PD at entrypoint address: %x\n", plan->pc_entry);
}

seL4_MessageInfo_t monitor_call_deploy_first_half(seL4_Word num_req_pc)
{
    pc_monitor_error err = monitor_check_deploy_num(num_req_pc);
    if (err != MON_NO_ERROR) {
        goto fh_exit;
    }

    err = monitor_reset_deploy_request(num_req_pc);
    if (err != MON_NO_ERROR) {
        goto fh_exit;
    }

    err = monitor_deploy_second_half();
    if (err != MON_NO_ERROR) {
        monitor_deploy_result_complete(err);
        goto fh_exit;
    }

    /* let the filesystem coroutine execute */
    microkit_cothread_yield();
fh_exit:
    return microkit_msginfo_new(err, 0);
}

seL4_MessageInfo_t monitor_call_deploy_to_protocon(seL4_Word pc_id,
                                                    seL4_Word peer_mask,
                                                    seL4_Word peers_all)
{
    seL4_Word valid_mask;
    pc_monitor_error err;

    if (pc_id >= ca_bootinfo.num_pc) {
        return microkit_msginfo_new(MON_INVALID_PC_ID, 0);
    }
    if (!protocon_state_check_lifecycle_state(pc_id, PROTOCON_PASSIVE)) {
        return microkit_msginfo_new(MON_PC_UNAVAILABLE, 0);
    }
    if (peers_all > 1) {
        return microkit_msginfo_new(MON_INVALID_PEER_SET, 0);
    }

    valid_mask = ((seL4_Word)1U << ca_bootinfo.num_pc) - 1U;
    if (peers_all) {
        peer_mask = valid_mask & ~((seL4_Word)1U << pc_id);
    } else if (peer_mask & ~valid_mask) {
        return microkit_msginfo_new(MON_INVALID_PEER_SET, 0);
    }
    if (peer_mask & ((seL4_Word)1U << pc_id)) {
        return microkit_msginfo_new(MON_INVALID_PEER_SET, 0);
    }
    if (deploy_request_active) {
        return microkit_msginfo_new(MON_FAIL_TO_DEPLOY, 0);
    }

    deploy_request.num_req_pc = 1;
    deploy_request.target_pc = (uint32_t)pc_id;
    deploy_request.requested_peer_mask = (uint32_t)peer_mask;
    deploy_request.targeted = true;
    deploy_request_active = true;
    monitor_deploy_result_begin(1);

    err = monitor_deploy_second_half();
    if (err != MON_NO_ERROR) {
        monitor_deploy_result_complete(err);
        return microkit_msginfo_new(err, 0);
    }
    microkit_cothread_yield();
    return microkit_msginfo_new(MON_NO_ERROR, 0);
}

seL4_MessageInfo_t monitor_call_deploy_result(void)
{
    if (!deploy_result.ready) {
        return microkit_msginfo_new(MON_DEPLOY_IN_PROGRESS, 0);
    }

    const pc_deploy_result_t *response = &deploy_result.response;

    microkit_mr_set(PC_DEPLOY_RESULT_REQUESTED_COUNT, response->requested_count);
    microkit_mr_set(PC_DEPLOY_RESULT_DEPLOYED_COUNT, response->deployed_count);
    microkit_mr_set(PC_DEPLOY_RESULT_PROTOCON_MASK, response->protocon_mask);
    microkit_mr_set(PC_DEPLOY_RESULT_CONNECTED_PEER_MASK, response->connected_peer_mask);
    microkit_mr_set(PC_DEPLOY_RESULT_REJECTED_PEER_MASK, response->rejected_peer_mask);
    microkit_mr_set(PC_DEPLOY_RESULT_UNAVAILABLE_PEER_MASK,
                    response->unavailable_peer_mask);
    return microkit_msginfo_new(deploy_result.error, PC_DEPLOY_RESULT_MR_COUNT);
}

static pc_monitor_error protocon_deploy_to_protocon(payload_info_t *info, uint32_t pc_id)
{
    deploy_plan_t plan = {0};
    protocon_svc_req_t req = {0};
    uint64_t started_at = monitor_profile_counter();

    service_manifest_parse(info, &req);
    service_planner_select_protocon_by_id(&req, &plan, protocon_states, pc_id);

    pc_monitor_error err = protocon_deploy_plan_check(&plan);
    if (err != MON_NO_ERROR) {
        monitor_profile_begin_deploy(pc_id, started_at);
        monitor_profile_fail(pc_id);
        return err;
    }
    monitor_profile_begin_deploy(pc_id, started_at);
    monitor_profile_record(pc_id, PC_PROFILE_DEPLOY_PARSE_PLAN, started_at);

    return protocon_deploy_execute(info, &plan);
}

static pc_monitor_error protocon_deploy_execute(payload_info_t *info, deploy_plan_t *plan)
{
    unsigned int pc_id = (unsigned int)plan->pc_id;

    /* Copy the payload and symbol metadata into the Protocon image region. */
    MONITOR_PROFILE_TRACE_SET(payload_copy);
    {
        protocon_pre_instantiate(plan, info);
    }
    MONITOR_PROFILE_TRACE_RECORD(payload_copy, pc_id, PC_PROFILE_DEPLOY_PAYLOAD_COPY);

    /* Resolve and install the services requested by the payload. */
    MONITOR_PROFILE_TRACE_SET(service_install);
    {
        if (!service_installer_apply(plan)) {
            monitor_profile_fail(pc_id);
            return MON_FAIL_TO_DEPLOY;
        }
    }
    MONITOR_PROFILE_TRACE_RECORD(service_install, pc_id, PC_PROFILE_DEPLOY_SERVICE_INSTALL);

    /* Initialise the trusted loader state and start the Protocon. */
    MONITOR_PROFILE_TRACE_SET(protocon_start);
    {
        protocon_start(plan);
    }
    MONITOR_PROFILE_TRACE_RECORD(protocon_start, pc_id, PC_PROFILE_DEPLOY_START);

    monitor_profile_mark_running(pc_id);

    return MON_NO_ERROR;
}
