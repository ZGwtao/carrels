/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <sddf/network/config.h>
#include <sddf/network/vswitch.h>

extern net_vswitch_orchestrator_config_t net_vswitch_orchestrator_config;

static uint32_t peer_policy[PC_CHILD_PER_MONITOR_MAX_NUM];
static uint32_t monitored_protocon_count;

seL4_MessageInfo_t monitor_call_set_vswitch_acl(seL4_Word port0,
                                                seL4_Word port1,
                                                seL4_Word allow)
{
    microkit_msginfo reply;
    vswitch_err_t error;

    microkit_mr_set(VSWITCH_ACL_PORT0, port0);
    microkit_mr_set(VSWITCH_ACL_PORT1, port1);
    microkit_mr_set(VSWITCH_ACL_VALUE, allow);
    reply = microkit_ppcall(
        net_vswitch_orchestrator_config.vswitch_id,
        microkit_msginfo_new(VSWITCH_SET_ACL, VSWITCH_ACL_NUM_ARGS));

    error = microkit_mr_get(VSWITCH_ACL_RET_ERR);
    if (microkit_msginfo_get_label(reply) != 0 || error != VSWITCH_ERR_OKAY) {
        microkit_mr_set(0, error);
        return microkit_msginfo_new(MON_VSWITCH_ERROR, 1);
    }

    return microkit_msginfo_new(MON_NO_ERROR, 0);
}

void monitor_acl_initialise(uint32_t num_protocons)
{
    monitored_protocon_count = num_protocons;
    memset(peer_policy, 0, sizeof(peer_policy));
}

void monitor_acl_release_protocon(uint32_t pc_id)
{
    if (pc_id >= monitored_protocon_count) {
        return;
    }

    for (uint32_t peer = 0; peer < monitored_protocon_count; ++peer) {
        if (peer == pc_id) {
            continue;
        }
        (void)monitor_call_set_vswitch_acl(pc_id, peer, 0);
        peer_policy[peer] &= ~((uint32_t)1U << pc_id);
    }
    peer_policy[pc_id] = 0;
}

pc_monitor_error monitor_acl_apply_deployment(uint32_t pc_id,
                                              uint32_t requested_peer_mask,
                                              uint32_t *connected_peer_mask,
                                              uint32_t *rejected_peer_mask,
                                              uint32_t *unavailable_peer_mask)
{
    uint32_t connected = 0;
    uint32_t rejected = 0;
    uint32_t unavailable = 0;

    if (pc_id >= monitored_protocon_count) {
        return MON_INVALID_PC_ID;
    }

    peer_policy[pc_id] = requested_peer_mask;
    for (uint32_t peer = 0; peer < monitored_protocon_count; ++peer) {
        uint32_t peer_bit = (uint32_t)1U << peer;

        if (!(requested_peer_mask & peer_bit)) {
            continue;
        }
        if (!protocon_state_check_lifecycle_state(peer, PROTOCON_ACTIVE) &&
            !protocon_state_check_lifecycle_state(peer, PROTOCON_HANG)) {
            unavailable |= peer_bit;
            continue;
        }
        if (!(peer_policy[peer] & ((uint32_t)1U << pc_id))) {
            rejected |= peer_bit;
            continue;
        }
        if (microkit_msginfo_get_label(monitor_call_set_vswitch_acl(pc_id, peer, 1)) !=
            MON_NO_ERROR) {
            return MON_VSWITCH_ERROR;
        }
        connected |= peer_bit;
    }

    *connected_peer_mask = connected;
    *rejected_peer_mask = rejected;
    *unavailable_peer_mask = unavailable;
    return MON_NO_ERROR;
}
