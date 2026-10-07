/*
 * SPDX-FileCopyrightText: 2026 UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include_next <string.h>

/* Missing from the sDDF custom libc. */
char *strchr(const char *s, int c);
