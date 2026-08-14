/**
 * \file            example_misc.h
 * \brief           Platform independent interface for implementing some
 *                  specific function for AVR, linux PC or ARM
 */

/*
 * Copyright (c) 2011 Eugene SAMOYLOV
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef MICRORL_EXAMPLE_MISC_HDR_H
#define MICRORL_EXAMPLE_MISC_HDR_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <microrl.h>

int    print(microrl_t* mrl, const char* str);
char   get_char(void);
int    execute(microrl_t* mrl, int argc, const char* const *argv);
char** complete(microrl_t* mrl, int argc, const char* const *argv);
void   sigint(microrl_t* mrl);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MICRORL_EXAMPLE_MISC_HDR_H */
