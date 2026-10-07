# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

PC_ORCHESTRATOR_OBJS := \
	$(PC_FS_HELPERS_OBJ) \
	pc/orchestrator/orchestrator.o \
	pc/orchestrator/cri_runtime.o \
	pc/util/pico_vfs.o \
	pc/microrl.o

vpath orchestrator/%.c $(PC_SRC_DIR)/src

pc/microrl.o: CFLAGS := $(PC_CFLAGS) $(CFLAGS) \
	-I$(PC_MICRORL_SRC_DIR)/include

pc/microrl.o: $(PC_MICRORL_SRC_DIR)/microrl.c | pc
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

vsock_backend.elf: LDFLAGS += -L$(BOARD_DIR)/lib
vsock_backend.elf: $(PC_ORCHESTRATOR_OBJS) \
		$(PC_LIBMICROKITCO_OBJ) pc/$(PC_LIBTRUSTEDLO_OBJ) libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
