/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SCHED_BENCH_CONTROL_VADDR 0x0C000000
#define SCHED_BENCH_CONTROL_MAGIC 0x53434844U
#define SCHED_BENCH_VICTIM_CHANNEL 31

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t completed_generation;
    bool active;
    uint64_t iterations;
} sched_bench_control_t;

static inline sched_bench_control_t *sched_bench_control(void)
{
    return (sched_bench_control_t *)SCHED_BENCH_CONTROL_VADDR;
}

static inline void sched_bench_victim_ready(sched_bench_control_t *control)
{
    __atomic_store_n(&control->magic, SCHED_BENCH_CONTROL_MAGIC, __ATOMIC_RELEASE);
}

static inline bool sched_bench_victim_is_ready(const sched_bench_control_t *control)
{
    return __atomic_load_n(&control->magic, __ATOMIC_ACQUIRE) ==
        SCHED_BENCH_CONTROL_MAGIC;
}

static inline void sched_bench_start_victim(sched_bench_control_t *control,
                                             uint32_t generation)
{
    __atomic_store_n(&control->generation, generation, __ATOMIC_RELAXED);
    __atomic_store_n(&control->active, true, __ATOMIC_RELEASE);
}

static inline bool
sched_bench_victim_is_active(const sched_bench_control_t *control)
{
    return __atomic_load_n(&control->active, __ATOMIC_ACQUIRE);
}

static inline uint32_t
sched_bench_victim_generation(const sched_bench_control_t *control)
{
    return __atomic_load_n(&control->generation, __ATOMIC_ACQUIRE);
}

static inline void sched_bench_stop_victim(sched_bench_control_t *control)
{
    __atomic_store_n(&control->active, false, __ATOMIC_RELEASE);
}

static inline void sched_bench_victim_completed(sched_bench_control_t *control,
                                                 uint32_t generation)
{
    __atomic_store_n(&control->completed_generation, generation, __ATOMIC_RELEASE);
}

static inline bool
sched_bench_victim_has_completed(const sched_bench_control_t *control,
                                  uint32_t generation)
{
    return __atomic_load_n(&control->completed_generation, __ATOMIC_ACQUIRE) ==
        generation;
}

static inline void sched_bench_iteration_completed(sched_bench_control_t *control)
{
    __atomic_fetch_add(&control->iterations, 1, __ATOMIC_RELAXED);
}

static inline uint64_t sched_bench_iterations(const sched_bench_control_t *control)
{
    return __atomic_load_n(&control->iterations, __ATOMIC_RELAXED);
}

