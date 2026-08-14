/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <sel4/sel4.h>

typedef enum {
    MON_NO_ERROR = seL4_NoError,
    MON_DUMMY_ERROR = seL4_NumErrors + 1,
    MON_INVALID_PC_ID,
    MON_INVALID_REQ_PC_NUM,
    MON_FAIL_TO_INIT_COROUTINE,
    MON_FAIL_TO_DEPLOY,
    MON_NO_AVAIL_PC,
} pc_monitor_error;
