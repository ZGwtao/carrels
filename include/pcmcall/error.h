#pragma once

#include <sel4/sel4.h>


typedef enum {
    mon_NoError = seL4_NoError,
    mon_DummyError = seL4_NumErrors + 1,
    mon_InvalidPCId,
    mon_InvalidReqPCNum,
    mon_FailToInitCoroutine,
    mon_FailToInitStorage,
    mon_FailToDeploy,
    mon_NoAvailPc,
} pc_monitor_Error;
