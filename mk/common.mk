# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

PC_CONFIG_DIR := $(PC_SRC_DIR)/config
PC_HEADER_DIR := $(PC_SRC_DIR)/include
PC_TOOL_DIR := $(PC_SRC_DIR)/tools
PC_MICRORL_SRC_DIR := $(PC_SRC_DIR)/microrl
PC_LIBMICROKITCO_DIR := $(LIBMICROKITCO_PATH)
PC_BUILD_DIR_GEN := $(BUILD_DIR)/pc/generated

PC_MONITOR_VM_LAYOUT := $(PC_CONFIG_DIR)/monitor_vm_layout.py
PC_MONITOR_VM_LAYOUT_GEN := $(PC_TOOL_DIR)/gen_vm_layout.py
PC_MONITOR_VM_LAYOUT_HEADER := $(PC_BUILD_DIR_GEN)/monitor_vm_layout.h

PC_CFLAGS := \
	-I$(CONTAINER_LIBC_INCLUDE) \
	-I$(PC_HEADER_DIR) \
	-I$(PC_SRC_DIR) \
	-I$(PC_MICRORL_SRC_DIR)/include \
	-I$(PC_LIBTRUSTEDLO_DIR)/include \
	-I$(PC_LIBMICROKITCO_DIR) \
	-I$(PC_BUILD_DIR_GEN) \
	-I$(PC_TSLDR_BUILD_DIR_GEN)

LIBMICROKITCO_CFLAGS_pc := $(PC_CFLAGS)
PC_LIBMICROKITCO_OBJ := libmicrokitco_pc.a
PC_FS_HELPERS_OBJ := pc/fs/helpers.o

pc:
	mkdir -p $@

$(PC_MONITOR_VM_LAYOUT_HEADER): pc \
		$(PC_MONITOR_VM_LAYOUT) $(PC_MONITOR_VM_LAYOUT_GEN)
	@mkdir -p $(dir $@)
	python3 -B $(PC_MONITOR_VM_LAYOUT_GEN) \
		--config $(PC_MONITOR_VM_LAYOUT) \
		--header-output $@

vpath util/%.c $(PC_SRC_DIR)/src

pc/%.o: CFLAGS := $(PC_CFLAGS) $(CFLAGS)

pc/%.o: %.c | pc $(PC_MONITOR_VM_LAYOUT_HEADER) pc/$(PC_LIBTRUSTEDLO_OBJ)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

pc/fs/helpers.o: $(PC_SRC_DIR)/lib/fs/helpers/helpers.c | pc \
		$(PC_MONITOR_VM_LAYOUT_HEADER) pc/$(PC_LIBTRUSTEDLO_OBJ)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@
