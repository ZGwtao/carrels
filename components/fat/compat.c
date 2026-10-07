/*
 * SPDX-FileCopyrightText: 2026 UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>

/* Missing from the sDDF custom libc, but required by FatFs. */
char *strchr(const char *s, int c)
{
    while (*s != (char)c) {
        if (*s++ == '\0') {
            return NULL;
        }
    }
    return (char *)s;
}
