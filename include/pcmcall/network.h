/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>

seL4_MessageInfo_t monitor_call_set_vswitch_acl(seL4_Word port0,
                                                seL4_Word port1,
                                                seL4_Word allow);
