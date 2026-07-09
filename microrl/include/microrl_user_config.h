/**
 * \file microrl_user_config.h
 * \brief MicroRL application configuration for the carrels orchestrator.
 */

#ifndef MICRORL_HDR_USER_CONFIG_H
#define MICRORL_HDR_USER_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Long enough for an ELF path plus command arguments. */
#define MICRORL_CFG_CMDLINE_LEN          128
#define MICRORL_CFG_CMD_TOKEN_NMB        8

#define MICRORL_CFG_PROMPT_STRING        "orche@>$ "
#define MICRORL_CFG_USE_PROMPT_COLOR     0

#define MICRORL_CFG_USE_COMPLETE         1
#define MICRORL_CFG_USE_QUOTING          1

#define MICRORL_CFG_USE_ECHO_OFF         0

#define MICRORL_CFG_USE_HISTORY          1
#define MICRORL_CFG_RING_HISTORY_LEN     128

#define MICRORL_CFG_PRINT_BUFFER_LEN     64
#define MICRORL_CFG_USE_ESC_SEQ          1
#define MICRORL_CFG_USE_LIBC_STDIO       0
#define MICRORL_CFG_USE_CARRIAGE_RETURN  1
#define MICRORL_CFG_USE_CTRL_C           1
#define MICRORL_CFG_PROMPT_ON_INIT       0
#define MICRORL_CFG_END_LINE             "\r\n"

/* Do not depend on post_exec_hook() from the Unix demonstration file. */
#define MICRORL_CFG_USE_COMMAND_HOOKS    0

#ifdef __cplusplus
}
#endif

#endif /* MICRORL_HDR_USER_CONFIG_H */
