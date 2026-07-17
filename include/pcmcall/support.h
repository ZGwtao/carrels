#pragma once

#include <microkit.h>
#include <pcmcall/error.h>


seL4_MessageInfo_t
monitor_call_backup_protocon_loading_context(microkit_channel ch);


void monitor_main_load_trustedlo(uint32_t cid);

