#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Build the LionsOS FAT server against sDDF's small custom libc.  All FAT,
# FF15, FS server, configuration, and protocol sources remain owned by the
# LionsOS submodule; this directory contains only the libc compatibility shim.

LIONSOS_FAT_DIR := $(LIONSOS)/components/fs/fat
LIONSOS_FF15_DIR := $(LIONSOS)/dep/ff15
LIONSOS_FS_SERVER_DIR := $(LIONSOS)/lib/fs/server

FAT_CFLAGS := \
	-I$(FAT)/include \
	-I$(FAT_LIBC_INCLUDE) \
	-I$(LIONSOS)/include \
	-I$(LIBMICROKITCO_PATH) \
	-I$(LIONSOS_FF15_DIR) \
	-I$(LIONSOS_FAT_DIR)/config

LIBMICROKITCO_CFLAGS_fat := $(FAT_CFLAGS)
$(BUILD_DIR)/libmicrokitco_fat/libco.o \
$(BUILD_DIR)/libmicrokitco_fat/libmicrokitco.o: CFLAGS := $(FAT_CFLAGS) $(CFLAGS)

FAT_OBJ := \
	fat/ff15/ff.o \
	fat/ff15/ffunicode.o \
	fat/event.o \
	fat/op.o \
	fat/io.o \
	fat/compat.o

LIB_FS_SERVER_OBJ := $(addprefix lib/fs/server/, fd.o memory.o)
LIB_FS_SERVER_CFLAGS := \
	-I$(FAT)/include \
	-I$(FAT_LIBC_INCLUDE) \
	-I$(LIONSOS)/include

CHECK_FAT_FLAGS_MD5 := .fat_cflags-$(shell echo -- $(CFLAGS) $(FAT_CFLAGS) | shasum | sed 's/ *-//')

$(CHECK_FAT_FLAGS_MD5):
	-rm -f .fat_cflags-*
	touch $@

fat fat/ff15 lib/fs/server:
	mkdir -p $@

fat/ff15/%.o: CFLAGS := $(FAT_CFLAGS) $(CFLAGS)
fat/ff15/%.o: $(LIONSOS_FF15_DIR)/%.c $(FAT_LIBC_INCLUDE) $(CHECK_FAT_FLAGS_MD5) |fat/ff15
	$(CC) -c $(CFLAGS) $< -o $@

fat/compat.o: CFLAGS := $(FAT_CFLAGS) $(CFLAGS)
fat/compat.o: $(FAT)/compat.c $(FAT_LIBC_INCLUDE) $(CHECK_FAT_FLAGS_MD5) |fat
	$(CC) -c $(CFLAGS) $< -o $@

fat/%.o: CFLAGS := $(FAT_CFLAGS) $(CFLAGS)
fat/%.o: $(LIONSOS_FAT_DIR)/%.c $(FAT_LIBC_INCLUDE) $(CHECK_FAT_FLAGS_MD5) |fat
	$(CC) -c $(CFLAGS) $< -o $@

lib/fs/server/%.o: CFLAGS := $(LIB_FS_SERVER_CFLAGS) $(CFLAGS)
lib/fs/server/%.o: $(LIONSOS_FS_SERVER_DIR)/%.c $(FAT_LIBC_INCLUDE) |lib/fs/server
	$(CC) -c $(CFLAGS) $< -o $@

lib_fs_server.a: $(LIB_FS_SERVER_OBJ)
	$(AR) crv $@ $^
	$(RANLIB) $@

fat.elf: $(FAT_OBJ) libmicrokitco_fat.a lib_fs_server.a libsddf_util_debug.a
	$(LD) -L$(BOARD_DIR)/lib $^ -lmicrokit -Tmicrokit.ld -o $@

-include $(FAT_OBJ:.o=.d) $(LIB_FS_SERVER_OBJ:.o=.d)
