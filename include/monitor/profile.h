/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>

#include <microkit.h>

#include <carrels-user.h>
#include <pcmcall/error.h>

typedef struct {
    uint64_t generation;

    /* The state that matches with lifecycle state */
    pc_profile_state_t state;

    /* The beginning of a new lifecycle */
    uint64_t deploy_started_at;

    /* When the application is successfully loaded & started */
    uint64_t protocon_started_at;

    /* If the application call for "termination" */
    uint64_t terminate_received_at;

    /* The timestamp when the protocon is recycled */
    uint64_t reclaim_completed_at;

    /* Timestamps for latency breakdown analysis */
    uint64_t metrics[PC_PROFILE_COUNT];
} pc_profile_context_t;

extern pc_profile_context_t monitor_profile_contexts[];

static inline uint64_t monitor_profile_counter(void)
{
#if !defined(CARRELS_PROFILE)
    return 0;
#elif defined(CONFIG_ARCH_AARCH64)
    uint64_t value;
    __asm__ volatile("isb; mrs %0, pmccntr_el0" : "=r"(value) :: "memory");
    return value;
#elif defined(CONFIG_ARCH_X86_64)
    uint32_t low;
    uint32_t high;
    /* RDTSC is available on every x86-64 CPU, including QEMU's qemu64
     * model. LFENCE orders the read without requiring RDTSCP support. */
    __asm__ volatile("lfence; rdtsc; lfence" : "=a"(low), "=d"(high) :: "memory");
    return ((uint64_t)high << 32) | low;
#else
#error "Unsupported architecture for monitor profiling"
#endif
}

static inline pc_profile_context_t *monitor_profile_context(unsigned int pc_id)
{
    return &monitor_profile_contexts[pc_id];
}

static inline void monitor_profile_set(unsigned int pc_id,
                                       unsigned int metric,
                                       uint64_t cycles)
{
    if (metric < PC_PROFILE_COUNT) {
        monitor_profile_context(pc_id)->metrics[metric] = cycles;
    }
}

static inline void monitor_profile_record(unsigned int pc_id,
                                          unsigned int metric,
                                          uint64_t started_at)
{
    monitor_profile_set(pc_id, metric, monitor_profile_counter() - started_at);
}

/*
 * Inspired by seL4 kernel's tracepoint design:
 * ============================================
 * Declare a named, function-local trace point and later record the elapsed
 * cycles from it. Keeping the timestamp local prevents overlapping Protocon
 * lifecycles from sharing temporary profiling state.
 */
#define MONITOR_PROFILE_TRACE_SET(name) \
    uint64_t monitor_profile_trace_##name = monitor_profile_counter()

#define MONITOR_PROFILE_TRACE_RESET(name) \
    (monitor_profile_trace_##name = monitor_profile_counter())

#define MONITOR_PROFILE_TRACE_RECORD(name, pc_id, metric) \
    monitor_profile_record((pc_id), (metric), monitor_profile_trace_##name)

void monitor_profile_begin_deploy(unsigned int pc_id, uint64_t started_at);
void monitor_profile_mark_running(unsigned int pc_id);
void monitor_profile_mark_terminate(unsigned int pc_id);
void monitor_profile_complete_reclaim(unsigned int pc_id);
void monitor_profile_fail(unsigned int pc_id);

seL4_MessageInfo_t monitor_call_profile(seL4_Word pc_id, seL4_Word metric);
