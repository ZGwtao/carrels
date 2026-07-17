#pragma once

#include <microkit.h>
#include <monitor.h>




seL4_MessageInfo_t monitor_call_list_protocons(void);


seL4_MessageInfo_t monitor_call_query_protocons(microkit_channel ch);

