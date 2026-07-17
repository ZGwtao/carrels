#pragma once

#include <microkit.h>
#include <monitor.h>


seL4_MessageInfo_t
monitor_call_restore_protocon(microkit_channel ch);

seL4_MessageInfo_t
monitor_call_stop_and_restore_protocon(microkit_channel ch);
