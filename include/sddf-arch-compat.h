/*
 * SPDX-FileCopyrightText: 2026 UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/*
 * The pinned sDDF checkpoint implements util/cspace.c and util/vspace.c with
 * x86 seL4 API names even though the underlying operations are architectural
 * equivalents. Keep that dependency-specific workaround out of container.mk.
 *
 * AArch64 uses one PageTable object/API at every translation-table level, so
 * the x86 PageTable/PageDirectory/PDPT operations all map to that ARM API.
 * sDDF's PageDirectory object case is distinct from its PageTable case, so a
 * named value outside the seL4 object-type range keeps it unreachable.
 */
#if defined(__aarch64__)
#define SDDF_COMPAT_UNUSED_OBJECT_TYPE 0xffff

#define seL4_X86_4K seL4_ARM_SmallPageObject
#define seL4_X86_LargePageObject seL4_ARM_LargePageObject
#define seL4_X86_PageTableObject seL4_ARM_PageTableObject
#define seL4_X86_PageDirectoryObject SDDF_COMPAT_UNUSED_OBJECT_TYPE
#define seL4_X86_PDPTObject seL4_ARM_PageTableObject

#define seL4_X86_Page_Map seL4_ARM_Page_Map
#define seL4_X86_PageTable_Map seL4_ARM_PageTable_Map
#define seL4_X86_PageDirectory_Map seL4_ARM_PageTable_Map
#define seL4_X86_PDPT_Map seL4_ARM_PageTable_Map
#define seL4_X86_Default_VMAttributes seL4_ARM_Default_VMAttributes
#define SEL4_MAPPING_LOOKUP_NO_PDPT SEL4_MAPPING_LOOKUP_NO_PUD
#endif

