/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>
#include <pcmcall/error.h>

seL4_MessageInfo_t monitor_call_list_protocons(void);

seL4_MessageInfo_t monitor_call_query_protocons(microkit_channel ch);

seL4_MessageInfo_t monitor_call_query_exit_reason(seL4_Word pc_id);
