/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * What even is this file?
 *
 * The purpose of this file is to include binaries that the VMM needs to setup,
 * such as the Linux kernel image, the DTB, and the initial RAM disk. What
 * happens is that the assembler includes the binary data in an ELF section
 * that we specify. We can then have global variables that essentially point to
 * where the data has been included.
 *
 * For each ELF section, we define a name and also some configuration. The
 * attributes "aw" is to say that the section is allocatable and that it is
 * writeable. We also have "@progbits", this is to indicate that the section
 * contains data (rather than code or something else).
 *
 */

#if defined(CARRELS_PROTOCON_PATH)

.section .carrels_pc, "aw", @progbits
.global __carrels_protocon_start, __carrels_protocon_end

__carrels_protocon_start:
    .incbin CARRELS_PROTOCON_PATH
__carrels_protocon_end:

#endif

#if defined(CARRELS_TRAMPOLINE_PATH)

.section .carrels_tpl, "aw", @progbits
.global __carrels_trampoline_start, __carrels_trampoline_end

__carrels_trampoline_start:
    .incbin CARRELS_TRAMPOLINE_PATH
__carrels_trampoline_end:

#endif
