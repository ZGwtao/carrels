/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>
#include <carrels-user.h>
#include <pcmcall/error.h>

seL4_MessageInfo_t monitor_call_restore_protocon(microkit_channel ch);

seL4_MessageInfo_t monitor_call_stop_and_restore_protocon(microkit_channel ch,
                                                          pc_exit_reason_t reason);
