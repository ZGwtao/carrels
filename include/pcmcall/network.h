/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>
#include <pcmcall/error.h>

seL4_MessageInfo_t monitor_call_set_vswitch_acl(seL4_Word port0,
                                                seL4_Word port1,
                                                seL4_Word allow);

void monitor_acl_initialise(uint32_t num_protocons);
void monitor_acl_release_protocon(uint32_t pc_id);
pc_monitor_error monitor_acl_apply_deployment(uint32_t pc_id,
                                              uint32_t requested_peer_mask,
                                              uint32_t *connected_peer_mask,
                                              uint32_t *rejected_peer_mask,
                                              uint32_t *unavailable_peer_mask);
