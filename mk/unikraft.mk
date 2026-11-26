# SPDX-FileCopyrightText: 2026 UNSW
# SPDX-License-Identifier: BSD-2-Clause

UK_ON_MK_DIR ?= $(ROOT)/dep/uk-on-mk
UKF_IMGS := \
	unikraft-c-nginx-client.img \
	unikraft-c-hello.img \
	unikraft-c-http.img \
	unikraft-c-fs.img \
	unikraft-sqlite.img \
	unikraft-nginx.img

ifneq ($(filter aarch64 x86_64,$(ARCH)),$(ARCH))
$(error Unsupported ARCH '$(ARCH)'; expected aarch64 or x86_64)
endif

UKF_MF ?= $(ROOT)/src/client/uk.mf
UKF_HELPER ?= $(ROOT)/tools/service-helper.py

UKF_FLAGS := \
	ARCH=$(ARCH) \
	SDDF=$(SDDF) \
	BOARD_DIR=$(BOARD_DIR) \
	BUILD_DIR=$(BUILD_DIR) \
	MICROKIT_SDK=$(MICROKIT_SDK) \
	MICROKIT_BOARD=$(MICROKIT_BOARD) \
	MICROKIT_CONFIG=$(MICROKIT_CONFIG)

unikraft-c-nginx-client.elf: libsddf_util.a
	$(MAKE) -f $(UK_ON_MK_DIR)/Makefile $(UKF_FLAGS) \
		TARGET_APP=c-nginx-client uk-catalog-build
	cp $(BUILD_DIR)/uk/c-nginx-client/c-nginx-client.elf $@

unikraft-c-nginx-client.img: unikraft-c-nginx-client.elf $(UKF_MF) $(UKF_HELPER)
	$(PYTHON) $(UKF_HELPER) --mf $(UKF_MF) --elf $< -o $@

unikraft-c-hello.elf: libsddf_util.a
	$(MAKE) -f $(UK_ON_MK_DIR)/Makefile $(UKF_FLAGS) \
		TARGET_APP=c-hello uk-catalog-build
	cp $(BUILD_DIR)/uk/c-hello/c-hello.elf $@

unikraft-c-hello.img: unikraft-c-hello.elf $(UKF_MF) $(UKF_HELPER)
	$(PYTHON) $(UKF_HELPER) --mf $(UKF_MF) --elf $< -o $@

unikraft-c-http.elf: libsddf_util.a
	$(MAKE) -f $(UK_ON_MK_DIR)/Makefile $(UKF_FLAGS) \
		TARGET_APP=c-http uk-catalog-build
	cp $(BUILD_DIR)/uk/c-http/c-http.elf $@

unikraft-c-http.img: unikraft-c-http.elf $(UKF_MF) $(UKF_HELPER)
	$(PYTHON) $(UKF_HELPER) --mf $(UKF_MF) --elf $< -o $@

unikraft-c-fs.elf: libsddf_util.a
	$(MAKE) -f $(UK_ON_MK_DIR)/Makefile $(UKF_FLAGS) \
		TARGET_APP=c-fs uk-catalog-build
	cp $(BUILD_DIR)/uk/c-fs/c-fs.elf $@

unikraft-c-fs.img: unikraft-c-fs.elf $(UKF_MF) $(UKF_HELPER)
	$(PYTHON) $(UKF_HELPER) --mf $(UKF_MF) --elf $< -o $@

unikraft-sqlite.elf: libsddf_util.a
	$(MAKE) -f $(UK_ON_MK_DIR)/Makefile $(UKF_FLAGS) \
		TARGET_APP=sqlite uk-catalog-build
	cp $(BUILD_DIR)/uk/sqlite/sqlite.elf $@

unikraft-sqlite.img: unikraft-sqlite.elf $(UKF_MF) $(UKF_HELPER)
	$(PYTHON) $(UKF_HELPER) --mf $(UKF_MF) --elf $< -o $@

unikraft-nginx.elf: libsddf_util.a
	$(MAKE) -f $(UK_ON_MK_DIR)/Makefile $(UKF_FLAGS) \
		TARGET_APP=nginx uk-catalog-build
	cp $(BUILD_DIR)/uk/nginx/nginx.elf $@

unikraft-nginx.img: unikraft-nginx.elf $(UKF_MF) $(UKF_HELPER)
	$(PYTHON) $(UKF_HELPER) --mf $(UKF_MF) --elf $< -o $@
