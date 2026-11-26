/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <microkit.h>
#include <libmicrokitco.h>

#include <lions/fs/config.h>
#include <lions/fs/helpers.h>
#include <lions/fs/protocol.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/util/printf.h>

#include <carrels-user.h>
#include <ioutils/pico_vfs.h>
#include <pcmcall/error.h>

#define MONITOR_CHANNEL 1
#define MONITOR_NOTIFICATION 30
#define PAYLOAD_VADDR 0x0b000000
#define COTHREAD_STACK_SIZE 0x10000

#ifndef WARMUP_COUNT
#define WARMUP_COUNT 100
#endif

#ifndef SAMPLE_COUNT
#define SAMPLE_COUNT 100
#endif

__attribute__((section(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((section(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((section(".fs_client_config"))) fs_client_config_t fs_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

static serial_queue_handle_t serial_tx_queue;
static co_control_t cothread_control;
static char cothread_stacks[2][COTHREAD_STACK_SIZE];

static const unsigned int batch_sizes[] = {1, 2, 4, 8, 16};

static const unsigned int reclaim_profile_metrics[] = {
    PC_PROFILE_RECLAIM_ACL,
    PC_PROFILE_RECLAIM_PD_STOP,
    PC_PROFILE_RECLAIM_LOADER_RESET,
    PC_PROFILE_RECLAIM_PAYLOAD_CLEAR,
    PC_PROFILE_RECLAIM_TOTAL,
    PC_PROFILE_RUNNING_TO_TERMINATE,
};

static const char *reclaim_profile_names[] = {
    "acl",
    "pd_stop",
    "loader_reset",
    "payload_clear",
    "reclaim_total",
    "running_to_terminate",
};

#define RECLAIM_PROFILE_COUNT \
    (sizeof(reclaim_profile_metrics) / sizeof(reclaim_profile_metrics[0]))

typedef struct {
    uint64_t deploy_ns;
    uint64_t first_completion_ns;
    uint64_t reclaim_ns;
    uint64_t lifecycle_ns;
    uint64_t profile_sum_cycles[RECLAIM_PROFILE_COUNT];
    uint64_t profile_max_cycles[RECLAIM_PROFILE_COUNT];
} latency_sample_t;

typedef struct {
    uint64_t minimum;
    uint64_t mean;
    uint64_t median;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    uint64_t variance;
    uint64_t standard_deviation;
} latency_statistics_t;

static uint64_t time_ns(void)
{
    return sddf_timer_time_now(timer_config.driver_id);
}

static void wait_for_monitor(void)
{
    microkit_cothread_wait_on_channel(MONITOR_NOTIFICATION);
}

static void initialise_ramfs(void)
{
    fs_cmpl_t completion;
    int error = fs_command_blocking(
        &completion,
        (fs_cmd_t){.type = FS_CMD_INITIALISE}
    );

    if (error || completion.status != FS_STATUS_SUCCESS) {
        sddf_printf("ELASTICITY_BENCH|ERROR|ramfs-initialise\n");
        microkit_internal_crash(error ? error : -1);
    }
}

static void load_payload(void)
{
    int error;
    pico_vfs_readfile2buf((void *)PAYLOAD_VADDR, "nop-native.img", &error);

    if (error != seL4_NoError) {
        sddf_printf("ELASTICITY_BENCH|ERROR|read-payload\n");
        microkit_internal_crash(error);
    }
}

static void request_batch_deployment(unsigned int instance_count)
{
    microkit_mr_set(0, PC_MONITOR_CALL_DEPLOY);
    microkit_mr_set(1, instance_count);
    microkit_msginfo info = microkit_ppcall(
        MONITOR_CHANNEL,
        microkit_msginfo_new(0, 2)
    );

    if (microkit_msginfo_get_label(info) != MON_NO_ERROR) {
        sddf_printf("ELASTICITY_BENCH|ERROR|deploy-request|batch=%u|error=%lu\n",
                    instance_count,
                    microkit_msginfo_get_label(info));
        microkit_internal_crash(-1);
    }
}

static uint32_t wait_for_batch_deployment(unsigned int instance_count)
{
    for (;;) {
        wait_for_monitor();

        microkit_mr_set(0, PC_MONITOR_CALL_DEPLOY_RESULT);
        microkit_msginfo info = microkit_ppcall(
            MONITOR_CHANNEL,
            microkit_msginfo_new(0, 1)
        );
        seL4_Word error = microkit_msginfo_get_label(info);

        if (error == MON_DEPLOY_IN_PROGRESS) {
            continue;
        }

        pc_deploy_result_t result = {
            .requested_count =
                (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_REQUESTED_COUNT),
            .deployed_count =
                (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_DEPLOYED_COUNT),
            .protocon_mask =
                (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_PROTOCON_MASK),
        };

        if (error != MON_NO_ERROR || result.requested_count != instance_count ||
            result.deployed_count != instance_count ||
            __builtin_popcount(result.protocon_mask) != instance_count) {
            sddf_printf("ELASTICITY_BENCH|ERROR|deploy-result|batch=%u|"
                        "requested=%u|deployed=%u|mask=0x%x|error=%lu\n",
                        instance_count,
                        result.requested_count,
                        result.deployed_count,
                        result.protocon_mask,
                        error);
            microkit_internal_crash(-1);
        }

        return result.protocon_mask;
    }
}

static uint32_t query_running_protocons(void)
{
    microkit_mr_set(0, PC_MONITOR_CALL_QUERY_PROTOCONS);
    microkit_msginfo info = microkit_ppcall(
        MONITOR_CHANNEL,
        microkit_msginfo_new(0, 1)
    );
    if (microkit_msginfo_get_label(info) != MON_NO_ERROR) {
        sddf_printf("ELASTICITY_BENCH|ERROR|query\n");
        microkit_internal_crash(-1);
    }
    return (uint32_t)microkit_mr_get(0);
}

static uint64_t wait_for_batch_reclamation(uint32_t batch_mask,
                                           uint64_t started,
                                           uint64_t *first_completion)
{
    bool recorded_first = false;

    for (;;) {
        uint32_t running = query_running_protocons() & batch_mask;
        uint64_t observed_at = time_ns();

        if (!recorded_first && running != batch_mask) {
            *first_completion = observed_at - started;
            recorded_first = true;
        }
        if (running == 0) {
            return observed_at;
        }
        wait_for_monitor();
    }
}

static uint64_t query_profile_metric(unsigned int pc_id, unsigned int metric)
{
    microkit_mr_set(0, PC_MONITOR_CALL_PROFILE);
    microkit_mr_set(1, pc_id);
    microkit_mr_set(2, metric);
    microkit_msginfo info = microkit_ppcall(
        MONITOR_CHANNEL,
        microkit_msginfo_new(0, 3)
    );

    if (microkit_msginfo_get_label(info) != MON_NO_ERROR ||
        microkit_mr_get(2) != PC_PROFILE_STATE_COMPLETE) {
        sddf_printf("ELASTICITY_BENCH|ERROR|profile|pc=%u|metric=%u|error=%lu\n",
                    pc_id,
                    metric,
                    microkit_msginfo_get_label(info));
        microkit_internal_crash(-1);
    }
    return microkit_mr_get(0);
}

static void collect_reclaim_profile(uint32_t batch_mask, latency_sample_t *sample)
{
    for (unsigned int pc_id = 0; pc_id < 16; pc_id++) {
        if ((batch_mask & ((uint32_t)1U << pc_id)) == 0) {
            continue;
        }

        for (unsigned int i = 0; i < RECLAIM_PROFILE_COUNT; i++) {
            uint64_t cycles = query_profile_metric(pc_id, reclaim_profile_metrics[i]);
            sample->profile_sum_cycles[i] += cycles;
            if (cycles > sample->profile_max_cycles[i]) {
                sample->profile_max_cycles[i] = cycles;
            }
        }
    }
}

static latency_sample_t run_iteration(unsigned int instance_count)
{
    uint64_t started = time_ns();
    request_batch_deployment(instance_count);
    uint32_t batch_mask = wait_for_batch_deployment(instance_count);
    uint64_t deployed = time_ns();

    uint64_t first_completion;
    uint64_t reclaimed = wait_for_batch_reclamation(
        batch_mask,
        started,
        &first_completion
    );

    latency_sample_t sample = {
        .deploy_ns = deployed - started,
        .first_completion_ns = first_completion,
        .reclaim_ns = reclaimed - deployed,
        .lifecycle_ns = reclaimed - started,
    };
    collect_reclaim_profile(batch_mask, &sample);
    return sample;
}

static void sort_samples(uint64_t *samples, unsigned int count)
{
    for (unsigned int i = 1; i < count; i++) {
        uint64_t value = samples[i];
        unsigned int j = i;

        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = value;
    }
}

static uint64_t integer_square_root(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = (uint64_t)1 << 62;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static latency_statistics_t calculate_statistics(uint64_t *samples,
                                                 unsigned int count)
{
    uint64_t sum = 0;
    for (unsigned int i = 0; i < count; i++) {
        sum += samples[i];
    }
    uint64_t mean = sum / count;

    uint64_t squared_deviation_sum = 0;
    for (unsigned int i = 0; i < count; i++) {
        uint64_t deviation = samples[i] > mean
            ? samples[i] - mean
            : mean - samples[i];
        squared_deviation_sum += deviation * deviation;
    }

    sort_samples(samples, count);
    uint64_t variance = squared_deviation_sum / count;
    return (latency_statistics_t){
        .minimum = samples[0],
        .mean = mean,
        .median = count % 2 == 0
            ? (samples[count / 2 - 1] + samples[count / 2]) / 2
            : samples[count / 2],
        .p95 = samples[(95 * count + 99) / 100 - 1],
        .p99 = samples[(99 * count + 99) / 100 - 1],
        .maximum = samples[count - 1],
        .variance = variance,
        .standard_deviation = integer_square_root(variance),
    };
}

static void report_statistics(unsigned int instance_count,
                              const char *metric,
                              uint64_t *samples)
{
    latency_statistics_t stats = calculate_statistics(samples, SAMPLE_COUNT);

    sddf_printf("ELASTICITY_BENCH|batch=%u|metric=%s|samples=%u|"
                "min_ns=%lu|mean_ns=%lu|median_ns=%lu|p95_ns=%lu|p99_ns=%lu|"
                "max_ns=%lu|variance_ns2=%lu|stddev_ns=%lu\n",
                instance_count,
                metric,
                SAMPLE_COUNT,
                stats.minimum,
                stats.mean,
                stats.median,
                stats.p95,
                stats.p99,
                stats.maximum,
                stats.variance,
                stats.standard_deviation);

    if (metric[0] == 'l') {
        uint64_t throughput_milli_per_second =
            (uint64_t)instance_count * 1000000000000ULL / stats.mean;
        sddf_printf("ELASTICITY_BENCH|batch=%u|metric=throughput|"
                    "milli_instances_per_second=%lu\n",
                    instance_count,
                    throughput_milli_per_second);
    }
}

static void report_profile_statistics(unsigned int instance_count,
                                      const char *metric,
                                      uint64_t *samples)
{
    latency_statistics_t stats = calculate_statistics(samples, SAMPLE_COUNT);

    sddf_printf("ELASTICITY_PROFILE|batch=%u|metric=%s|samples=%u|"
                "min_cycles=%lu|mean_cycles=%lu|median_cycles=%lu|"
                "p95_cycles=%lu|p99_cycles=%lu|max_cycles=%lu|"
                "variance_cycles2=%lu|stddev_cycles=%lu\n",
                instance_count,
                metric,
                SAMPLE_COUNT,
                stats.minimum,
                stats.mean,
                stats.median,
                stats.p95,
                stats.p99,
                stats.maximum,
                stats.variance,
                stats.standard_deviation);
}

static void benchmark_batch(unsigned int instance_count)
{
    latency_sample_t samples[SAMPLE_COUNT];
    uint64_t deploy_samples[SAMPLE_COUNT];
    uint64_t first_completion_samples[SAMPLE_COUNT];
    uint64_t reclaim_samples[SAMPLE_COUNT];
    uint64_t lifecycle_samples[SAMPLE_COUNT];
    uint64_t profile_sum_samples[RECLAIM_PROFILE_COUNT][SAMPLE_COUNT];
    uint64_t profile_max_samples[RECLAIM_PROFILE_COUNT][SAMPLE_COUNT];

    for (unsigned int i = 0; i < WARMUP_COUNT; i++) {
        (void)run_iteration(instance_count);
    }
    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        samples[i] = run_iteration(instance_count);
    }

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        deploy_samples[i] = samples[i].deploy_ns;
        first_completion_samples[i] = samples[i].first_completion_ns;
        reclaim_samples[i] = samples[i].reclaim_ns;
        lifecycle_samples[i] = samples[i].lifecycle_ns;
        for (unsigned int metric = 0; metric < RECLAIM_PROFILE_COUNT; metric++) {
            profile_sum_samples[metric][i] = samples[i].profile_sum_cycles[metric];
            profile_max_samples[metric][i] = samples[i].profile_max_cycles[metric];
        }
    }

    report_statistics(instance_count, "deploy", deploy_samples);
    report_statistics(instance_count, "first_completion", first_completion_samples);
    report_statistics(instance_count, "reclaim", reclaim_samples);
    report_statistics(instance_count, "lifecycle", lifecycle_samples);
    for (unsigned int metric = 0; metric < RECLAIM_PROFILE_COUNT; metric++) {
        char name[64];

        sddf_snprintf(name,
                      sizeof(name),
                      "profile_sum_%s_cycles",
                      reclaim_profile_names[metric]);
        report_profile_statistics(instance_count, name, profile_sum_samples[metric]);

        sddf_snprintf(name,
                      sizeof(name),
                      "profile_max_%s_cycles",
                      reclaim_profile_names[metric]);
        report_profile_statistics(instance_count, name, profile_max_samples[metric]);
    }
}

static void benchmark_run(void)
{
    initialise_ramfs();
    load_payload();

    for (unsigned int i = 0; i < sizeof(batch_sizes) / sizeof(batch_sizes[0]); i++) {
        benchmark_batch(batch_sizes[i]);
    }

    sddf_printf("ELASTICITY_BENCH|DONE\n");
    for (;;) {
        microkit_cothread_yield();
    }
}

static void blocking_wait(microkit_channel channel)
{
    microkit_cothread_wait_on_channel(channel);
}

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));

    serial_queue_init(&serial_tx_queue,
                      serial_config.tx.queue.vaddr,
                      serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue);

    fs_set_blocking_wait(blocking_wait);
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    stack_ptrs_arg_array_t stacks = {
        (uintptr_t)cothread_stacks[0],
        (uintptr_t)cothread_stacks[1],
    };
    microkit_cothread_init(&cothread_control, COTHREAD_STACK_SIZE, stacks);
    if (microkit_cothread_spawn(benchmark_run, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();
}

void notified(microkit_channel channel)
{
    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(channel);
}
