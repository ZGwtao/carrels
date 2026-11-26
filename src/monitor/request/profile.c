/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>

#include <carrels-monitor.h>

/* Each protocon has one lifecycle profiling context */
pc_profile_context_t monitor_profile_contexts[PC_CHILD_PER_MONITOR_MAX_NUM];

void monitor_profile_begin_deploy(unsigned int pc_id, uint64_t started_at)
{
    pc_profile_context_t *context = monitor_profile_context(pc_id);

    /* Update lifecycle counter */
    uint64_t generation = context->generation + 1;

    /* Reset the entire context for the new lifecycle */
    memset(context, 0, sizeof(*context));

    context->generation = generation;
    context->state = PC_PROFILE_STATE_DEPLOYING;
    context->deploy_started_at = started_at;
}

void monitor_profile_mark_running(unsigned int pc_id)
{
    pc_profile_context_t *context = monitor_profile_context(pc_id);

    context->state = PC_PROFILE_STATE_RUNNING;
    context->protocon_started_at = monitor_profile_counter();
    context->metrics[PC_PROFILE_DEPLOY_TOTAL] =
        context->protocon_started_at - context->deploy_started_at;
}

void monitor_profile_mark_terminate(unsigned int pc_id)
{
    pc_profile_context_t *context = monitor_profile_context(pc_id);

    context->terminate_received_at = monitor_profile_counter();
    context->metrics[PC_PROFILE_RUNNING_TO_TERMINATE] =
        context->terminate_received_at - context->protocon_started_at;
    context->state = PC_PROFILE_STATE_RECLAIMING;
}

void monitor_profile_complete_reclaim(unsigned int pc_id)
{
    pc_profile_context_t *context = monitor_profile_context(pc_id);

    context->reclaim_completed_at = monitor_profile_counter();
    context->metrics[PC_PROFILE_RECLAIM_TOTAL] =
        context->reclaim_completed_at - context->terminate_received_at;
    context->metrics[PC_PROFILE_LIFECYCLE_TOTAL] =
        context->reclaim_completed_at - context->deploy_started_at;
    context->state = PC_PROFILE_STATE_COMPLETE;
}

void monitor_profile_fail(unsigned int pc_id)
{
    monitor_profile_context(pc_id)->state = PC_PROFILE_STATE_FAILED;
}

seL4_MessageInfo_t monitor_call_profile(seL4_Word pc_id, seL4_Word metric)
{
    if (pc_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        return microkit_msginfo_new(MON_INVALID_PC_ID, 0);
    }
    if (metric >= PC_PROFILE_COUNT) {
        return microkit_msginfo_new(MON_INVALID_REQ_PC_NUM, 0);
    }

    const pc_profile_context_t *context = monitor_profile_context((unsigned int)pc_id);

    microkit_mr_set(0, context->metrics[metric]);
    microkit_mr_set(1, context->generation);
    microkit_mr_set(2, context->state);

    return microkit_msginfo_new(MON_NO_ERROR, 3);
}
