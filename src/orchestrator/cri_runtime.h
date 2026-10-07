/* SPDX-License-Identifier: BSD-2-Clause */
#pragma once

#include <stdint.h>

void cri_runtime_init(void);
void cri_runtime_notified(void);
void cri_runtime_deploy_complete(uint32_t pc_id, int result);

/* Implemented by the vsock backend's carrels adapter. */
int orchestrator_cri_start(const char *image, uint32_t *pc_id);
int orchestrator_cri_stop(uint32_t pc_id);
int orchestrator_cri_image_exists(const char *image);
