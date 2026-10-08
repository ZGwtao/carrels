# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

PC_WHOAMI_CLIENT_OBJS := \
	pc/client/whoami.o

PC_CLIENT_NAMES := \
	whoami

PC_CLIENT_ELFS := $(addsuffix .elf,$(PC_CLIENT_NAMES))
PC_CLIENT_IMGS := $(addsuffix .img,$(PC_CLIENT_NAMES))
PC_SERVICE_IMGS := $(PC_CLIENT_IMGS)
PC_SERVICE_MANIFEST := $(PC_SRC_DIR)/src/client/service.mf

vpath client/%.c $(PC_SRC_DIR)/src

$(PC_CLIENT_ELFS): LDFLAGS += -L$(BOARD_DIR)/lib

whoami.elf:      $(PC_WHOAMI_CLIENT_OBJS)

$(PC_CLIENT_ELFS): libsddf_util.a pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@

.PHONY: pc-images
pc-images: $(PC_SERVICE_IMGS)

$(PC_SERVICE_IMGS): %.img: %.elf $(PC_SERVICE_MANIFEST) \
		$(PC_TOOL_DIR)/service-helper.py
	PYTHONPATH=$(SDDF)/tools/meta:$$PYTHONPATH $(PYTHON) \
		$(PC_TOOL_DIR)/service-helper.py \
		--mf $(PC_SERVICE_MANIFEST) \
		--elf $< \
		-o $@
