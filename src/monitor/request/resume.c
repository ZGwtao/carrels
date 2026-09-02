/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>

seL4_MessageInfo_t monitor_call_resume_protocon(microkit_channel ch)
{
    microkit_channel target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for resume\n");
        return microkit_msginfo_new(MON_INVALID_PC_ID, 0);
    }
    microkit_channel cid_to_check = target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL;
    microkit_channel cid = monitor_get_pcid_from_ch(cid_to_check);
    if (cid == (INVALID_PC_ID)) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id to resume given with ch: %d\n", cid_to_check);
    } else {
        if (!protocon_state_check_lifecycle_state(cid, PROTOCON_SUSPENDED)) {
            TSLDR_DBG_PRINT(PROGNAME "Invalid PD state to resume!\n");
        } else {
            microkit_pd_resume(target_pd_id);
            SET_PROTOCON_AS_INSTANTIATED(cid)
        }
    }
    monitor_main_notify_orchestrator();
    return microkit_msginfo_new(MON_NO_ERROR, 0);
}
