#pragma once

#include <ossvc.h>

#include <pcmcall/deploy.h>
#include <pcmcall/query.h>
#include <pcmcall/resume.h>
#include <pcmcall/stop.h>
#include <pcmcall/support.h>
#include <pcmcall/suspend.h>

extern seL4_Word pd_io_acl_rule;



seL4_MessageInfo_t
monitor_main_handle_pccall(microkit_channel ch);

