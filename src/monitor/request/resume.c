
#include <carrels-monitor.h>


seL4_MessageInfo_t monitor_call_resume_protocon(microkit_channel ch)
{
    int target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for resume\n");
        return microkit_msginfo_new(mon_InvalidPCId, 0);
    }
    int cid_to_check = target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL;
    int cid = monitor_get_pcid_from_ch(cid_to_check);
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
