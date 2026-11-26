/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include "../../../include/carrels-user.h"

int uk_app_main(void)
{
    microkit_mr_set(0, PC_MONITOR_CALL_TERMINATE);
    (void)microkit_ppcall(15, microkit_msginfo_new(0, 1));

    for (;;) {
        __asm__ volatile("" ::: "memory");
    }
}
