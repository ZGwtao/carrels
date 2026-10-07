/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/fs/helpers.h>
#include <stdbool.h>

/* return the size of file mapped in the memory */
uint64_t pico_vfs_readfile2buf(void *buf, const char *path, int *err);
bool pico_vfs_file_exists(const char *path);
