#pragma once

// channels id for monitor PD to communicate with the orchestrator PD and the dynamic PDs (protocons)
#define PC_MONITOR_ORCHESTRATOR_CHANNEL (15)
#define PC_MONITOR_PROTOCON_BASE_CHANNEL (24)

// monitor call numbers
#define PC_MONITOR_CALL_DEPLOY (1)
#define PC_MONITOR_CALL_HANG (3)
#define PC_MONITOR_CALL_RESUME (4)
#define PC_MONITOR_CALL_LIST_PROTOCONS (5)
#define PC_MONITOR_CALL_QUERY_PROTOCONS (10)
#define PC_MONITOR_CALL_FLIP_ACL_RULE (17)
#define PC_MONITOR_CALL_BACKUP_CONTEXT (20)
#define PC_MONITOR_CALL_TERMINATE_EXT (6)
#define PC_MONITOR_CALL_TERMINATE (0x100)


seL4_MessageInfo_t monitor_call_restore_protocon(microkit_channel ch);


static inline void monitor_main_notify_orchestrator()
{
    microkit_notify(PC_MONITOR_ORCHESTRATOR_CHANNEL);
}

