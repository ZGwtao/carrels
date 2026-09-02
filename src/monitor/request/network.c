/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <sddf/network/config.h>
#include <sddf/network/vswitch.h>

extern net_vswitch_orchestrator_config_t net_vswitch_orchestrator_config;

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
