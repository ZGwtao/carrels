/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>
#include <pcmcall/error.h>

typedef struct {
    uint32_t num_req_pc;
    uint32_t target_pc;
    uint32_t requested_peer_mask;
    bool targeted;
} monitor_deploy_request_t;

typedef struct {
    uint32_t target_pc;
    uint32_t connected_peer_mask;
    uint32_t rejected_peer_mask;
    uint32_t unavailable_peer_mask;
    pc_monitor_error error;
    bool ready;
} monitor_deploy_result_t;

extern uint32_t req_pc_num;
extern bool deploy_request_active;
extern monitor_deploy_request_t deploy_request;
extern monitor_deploy_result_t deploy_result;
extern uintptr_t __carrels_payload_start;

seL4_MessageInfo_t monitor_call_deploy_first_half(seL4_Word num_req_pc);

/* Start a targeted deployment using the image in the orchestrator/monitor payload region. */
seL4_MessageInfo_t monitor_call_deploy_to_protocon(seL4_Word pc_id,
                                                    seL4_Word peer_mask,
                                                    seL4_Word peers_all);

/* Return the completed targeted deployment result in MR 0..3. */
seL4_MessageInfo_t monitor_call_deploy_result(void);

pc_monitor_error protocon_deploy(payload_info_t *info);
