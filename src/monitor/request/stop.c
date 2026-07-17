
#include <carrels-monitor.h>


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
