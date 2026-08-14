/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>
#include <pcmcall/error.h>

seL4_MessageInfo_t monitor_call_hang_protocon(microkit_channel ch);
