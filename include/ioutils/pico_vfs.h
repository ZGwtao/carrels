/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/fs/helpers.h>
#include <stdbool.h>
#include <stdint.h>

/* return the size of file mapped in the memory */
uint64_t pico_vfs_readfile2buf(void *buf, const char *path, int *err);
bool pico_vfs_file_exists(const char *path);

/* Return 0 on success, -2 when capacity is insufficient, or -1 for other failures. */
int pico_vfs_read_to_cache(void *cache, uint64_t capacity, const char *path, uint64_t *size);
