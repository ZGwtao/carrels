/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>

seL4_MessageInfo_t monitor_call_stop_and_restore_protocon(microkit_channel ch)
{
    microkit_channel target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for stop and restore\n");
        return microkit_msginfo_new(MON_INVALID_PC_ID, 0);
    }
    monitor_profile_mark_terminate((unsigned int)target_pd_id);

    /* Remove the Protocon's peer access rules. */
    MONITOR_PROFILE_TRACE_SET(acl_release);
    {
        monitor_acl_release_protocon(target_pd_id);
    }
    MONITOR_PROFILE_TRACE_RECORD(acl_release,
                                 (unsigned int)target_pd_id,
                                 PC_PROFILE_RECLAIM_ACL);

    /* Stop the Protocon PD before restoring its loader. */
    MONITOR_PROFILE_TRACE_SET(pd_stop);
    {
        microkit_pd_stop(target_pd_id);
    }
    MONITOR_PROFILE_TRACE_RECORD(pd_stop,
                                 (unsigned int)target_pd_id,
                                 PC_PROFILE_RECLAIM_PD_STOP);

    return monitor_call_restore_protocon(target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL);
}

seL4_MessageInfo_t monitor_call_restore_protocon(microkit_channel ch)
{
    microkit_channel cid = monitor_get_pcid_from_ch(ch);
    if (cid == (INVALID_PC_ID)) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id to restore given with ch: %d\n", ch);
    } else {
        SET_PROTOCON_AS_AVAILABLE(cid)
        monitor_main_load_trustedlo(cid);
        monitor_profile_complete_reclaim((unsigned int)cid);
    }
    monitor_main_notify_orchestrator();
    return microkit_msginfo_new(MON_NO_ERROR, 0);
}
