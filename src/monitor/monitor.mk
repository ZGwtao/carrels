# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

PC_MONITOR_OBJS := \
	$(PC_FS_HELPERS_OBJ) \
	pc/monitor/entry.o \
	pc/monitor/mcall.o \
	pc/monitor/fault/fault.o \
	pc/monitor/request/deploy.o \
	pc/monitor/request/network.o \
	pc/monitor/request/query.o \
	pc/monitor/request/resume.o \
	pc/monitor/request/stop.o \
	pc/monitor/request/support.o \
	pc/monitor/request/suspend.o \
	pc/monitor/init/minit.o \
	pc/monitor/service/service_installer.o \
	pc/monitor/service/service_manifest.o \
	pc/monitor/service/service_planner.o \
	pc/monitor/service/service_registry.o \
	pc/util/pico_vfs.o

vpath monitor/%.c $(PC_SRC_DIR)/src
vpath monitor/service/%.c $(PC_SRC_DIR)/src
vpath monitor/fault/%.c $(PC_SRC_DIR)/src
vpath monitor/init/%.c $(PC_SRC_DIR)/src
vpath monitor/io/%.c $(PC_SRC_DIR)/src
vpath monitor/request/%.c $(PC_SRC_DIR)/src

payloads.o: protocon.elf trampoline.elf
	cp $(PC_SRC_DIR)/src/monitor/package_payloads.S .
	$(CC) -c $(CFLAGS) \
		-DCARRELS_PROTOCON_PATH=\"$(BUILD_DIR)/protocon.elf\" \
		-DCARRELS_TRAMPOLINE_PATH=\"$(BUILD_DIR)/trampoline.elf\" \
		package_payloads.S -o $@

monitor.elf: LDFLAGS += -L$(BOARD_DIR)/lib
monitor.elf: $(PC_MONITOR_OBJS) pc/$(PC_LIBTRUSTEDLO_OBJ) \
		$(PC_LIBMICROKITCO_OBJ) libsddf_util.a payloads.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
