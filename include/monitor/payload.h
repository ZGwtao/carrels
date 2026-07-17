#pragma once

#ifndef MONITOR_PAYLOADS_H
#define MONITOR_PAYLOADS_H

#include <stddef.h>
#include <stdint.h>

extern const unsigned char __carrels_protocon_start[];
extern const unsigned char __carrels_protocon_end[];

extern const unsigned char __carrels_trampoline_start[];
extern const unsigned char __carrels_trampoline_end[];


static inline size_t
monitor_protocon_capacity(void)
{
    return (size_t)(
        __carrels_protocon_end - __carrels_protocon_start
    );
}

static inline size_t
monitor_trampoline_capacity(void)
{
    return (size_t)(
        __carrels_trampoline_end - __carrels_trampoline_start
    );
}

#endif