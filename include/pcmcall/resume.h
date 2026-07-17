#pragma once

#include <microkit.h>
#include <pcmcall/error.h>



seL4_MessageInfo_t
monitor_call_resume_protocon(microkit_channel ch);
