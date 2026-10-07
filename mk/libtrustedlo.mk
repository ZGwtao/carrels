# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

PC_LIBTRUSTEDLO_DIR := $(CARRELS)/dep/libtrustedlo
PC_LIBTRUSTEDLO_OBJ := libtrustedlo/libtrustedlo.a

PC_TSLDR_BUILD_DIR := $(BUILD_DIR)/pc/libtrustedlo
PC_TSLDR_BUILD_DIR_GEN := $(PC_TSLDR_BUILD_DIR)/generated
PC_TSLDR_VM_LAYOUT_HEADER := $(PC_TSLDR_BUILD_DIR_GEN)/tsldr_vm_layout.h

pc/$(PC_LIBTRUSTEDLO_OBJ): pc
	$(MAKE) -f $(PC_LIBTRUSTEDLO_DIR)/Makefile \
		LIBTRUSTEDLO_PATH=$(PC_LIBTRUSTEDLO_DIR) \
		TARGET=$(TARGET) \
		MICROKIT_SDK:=$(MICROKIT_SDK) \
		BUILD_DIR:=pc \
		MICROKIT_BOARD:=$(MICROKIT_BOARD) \
		MICROKIT_CONFIG:=$(MICROKIT_CONFIG) \
		CPU:=$(CPU) \
		LLVM:=1

protocon.elf: pc/$(PC_LIBTRUSTEDLO_OBJ)
	cp $(PC_TSLDR_BUILD_DIR)/loader.elf $@

trampoline.elf: pc/$(PC_LIBTRUSTEDLO_OBJ)
	cp $(PC_TSLDR_BUILD_DIR)/trampoline.elf $@
