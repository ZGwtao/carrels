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

static inline pc_monitor_error monitor_reset_deploy_request(seL4_Word num_req_pc)
{
    if (deploy_request_active) {
        TSLDR_DBG_PRINT(PROGNAME "Rejected deploy request: another deployment is still active\n");
        return MON_FAIL_TO_DEPLOY;
    }
    deploy_request.num_req_pc = (uint32_t)num_req_pc;
    req_pc_num = (uint32_t)num_req_pc;
    deploy_request_active = true;
    return MON_NO_ERROR;
}

static inline void monitor_deploy_refresh_request(void)
{
    req_pc_num = 0;
    deploy_request.num_req_pc = 0;
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
    uint64_t sym_size = mktsymb_bundle_size(sym);
    uint64_t sym_offset = sizeof(protocon_image_header_t);
    uint64_t elf_offset = sym_offset + sym_size;

    protocon_image_header_t *header = (protocon_image_header_t *)dest;

    header->magic = PROTOCON_IMAGE_MAGIC;
    header->reserved = 0;
    header->mktsymb_offset = sym_offset;
    header->mktsymb_size = sym_size;
    header->elf_offset = elf_offset;
    header->elf_size = elf_size;

    memcpy((void *)(dest + sym_offset), sym->base, sym_size);
    memcpy((void *)(dest + elf_offset), (const void *)src, elf_size);

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

    if (monitor_check_deploy_num(num_req_pc) != MON_NO_ERROR) {
        monitor_finish_deploy_request();
        return;
    }

    err = service_manifest_header_parse(&payload_info, (__carrels_payload_start));
    if (err != seL4_NoError) {
        monitor_finish_deploy_request();
        return;
    }

    for (uint32_t i = 0; i < num_req_pc; ++i) {
        if (protocon_deploy(&payload_info) != MON_NO_ERROR) {
            TSLDR_DBG_PRINT(PROGNAME "Failed to deploy container\n");
            break;
        }
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

static inline void protocon_start(deploy_plan_t *plan)
{
    protocon_init_txlo_info(plan);
    protocon_init_txlo_context(plan);

    mktxlo_privilege_template_pd(plan->pc_id);

    SET_PROTOCON_AS_INSTANTIATED(plan->pc_id)

    microkit_pd_restart(plan->pc_id, plan->pc_entry);
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
        goto fh_exit;
    }

    /* let the filesystem coroutine execute */
    microkit_cothread_yield();
fh_exit:
    return microkit_msginfo_new(err, 0);
}

pc_monitor_error protocon_deploy(payload_info_t *info)
{
    deploy_plan_t plan = {0};
    protocon_svc_req_t req = {0};
    pc_monitor_error err;

    (void)service_manifest_parse(info, &req);

    (void)service_planner_select_protocon(&req, &plan, protocon_states);

    err = protocon_deploy_plan_check(&plan);
    if (err != MON_NO_ERROR) {
        return err;
    }

    protocon_pre_instantiate(&plan, info);

    service_installer_apply(&plan);

    protocon_start(&plan);
    return err;
}
