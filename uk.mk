# SPDX-FileCopyrightText: 2026 UNSW
# SPDX-License-Identifier: BSD-2-Clause



PC_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
PC_TOOL_DIR := $(PC_SRC_DIR)/tools
PC_UNIKRAFT_MANIFEST := $(PC_SRC_DIR)/src/client/uk.mf

BM_UNIKRAFT_DIR := $(ROOT)/dep/unikraft
BM_CATALOG_CORE_DIR := $(ROOT)/dep/catalog-core
BM_UK_MKCPIO := $(BM_UNIKRAFT_DIR)/support/scripts/mkcpio
UK_CONFIG_DIR := $(ROOT)/config

BM_UK_MUSL_DIR := $(BM_CATALOG_CORE_DIR)/repos/libs/musl
BM_UK_SQLITE_DIR := $(BM_CATALOG_CORE_DIR)/repos/libs/sqlite
BM_UK_NGINX_DIR := $(BM_CATALOG_CORE_DIR)/repos/libs/nginx
BM_UK_LWIP_DIR := $(BM_CATALOG_CORE_DIR)/repos/libs/lwip

UK_APPS := sqlite nginx
UNIKERNELS := $(addprefix unikraft-,$(addsuffix .img,$(UK_APPS)))

UK_CONFIG_sqlite := uk-carrels-sqlite-arm.config
UK_LIBS_sqlite := $(BM_UK_MUSL_DIR):$(BM_UK_SQLITE_DIR):$(BM_UK_LWIP_DIR)

UK_CONFIG_nginx := uk-carrels-nginx-arm.config
UK_LIBS_nginx := $(BM_UK_MUSL_DIR):$(BM_UK_NGINX_DIR):$(BM_UK_LWIP_DIR)


define UK_APP_template

UK_APP_DIR_$(1) := $(BM_CATALOG_CORE_DIR)/$(1)
UK_BUILD_DIR_$(1) := $(BUILD_DIR)/uk/$(1)
UK_PAYLOAD_ELF_$(1) := $(1)_default-arm64
UK_BUILT_ELF_$(1) := $$(UK_BUILD_DIR_$(1))/$$(UK_PAYLOAD_ELF_$(1))
UK_CONFIG_SRC_$(1) := $(UK_CONFIG_DIR)/uk/$$(UK_CONFIG_$(1))
UK_CONFIGURED_$(1) := $$(UK_BUILD_DIR_$(1))/.configured
UK_ROOTFS_DIR_$(1) := $$(UK_APP_DIR_$(1))/rootfs
UK_INITRD_$(1) := $$(UK_APP_DIR_$(1))/initrd.cpio

UK_MAKE_ARGS_$(1) = A=$$(UK_APP_DIR_$(1)) O=$$(UK_BUILD_DIR_$(1)) L=$$(UK_LIBS_$(1)) \
	SDDF=$(SDDF) MICROKIT_SDK=$(MICROKIT_SDK) MICROKIT_BOARD=$(MICROKIT_BOARD) \
	MICROKIT_CONFIG=$(MICROKIT_CONFIG) BOARD_DIR=$(BOARD_DIR) \
	SDDF_UTIL_LIB=$(abspath libsddf_util.a)

.PHONY: uk-build-$(1) uk-initrd-$(1)

uk-build-$(1): $$(UK_CONFIGURED_$(1)) libsddf_util.a uk-initrd-$(1)
	$$(MAKE) -C $(BM_UNIKRAFT_DIR) $$(UK_MAKE_ARGS_$(1))
	cp $$(UK_BUILT_ELF_$(1)) unikraft-$(1).elf

uk-initrd-$(1):
	rm -f $$(UK_INITRD_$(1))
	$(BM_UK_MKCPIO) $$(UK_INITRD_$(1)) $$(UK_ROOTFS_DIR_$(1))

$$(UK_CONFIGURED_$(1)): $$(UK_CONFIG_SRC_$(1))
	mkdir -p $$(UK_BUILD_DIR_$(1))
	$$(MAKE) -C $(BM_UNIKRAFT_DIR) $$(UK_MAKE_ARGS_$(1)) distclean
	$$(MAKE) -C $(BM_UNIKRAFT_DIR) $$(UK_MAKE_ARGS_$(1)) UK_DEFCONFIG=$$(UK_CONFIG_SRC_$(1)) defconfig
	touch $$@

unikraft-$(1).elf: uk-build-$(1)

unikraft-$(1).img: unikraft-$(1).elf $(PC_UNIKRAFT_MANIFEST) $(PC_TOOL_DIR)/service-helper.py
	PYTHONPATH=$(SDDF)/tools/meta:$$$$PYTHONPATH $(PYTHON) $(PC_TOOL_DIR)/service-helper.py \
		--mf $(PC_UNIKRAFT_MANIFEST) --elf $$< -o $$@

endef

$(foreach app,$(UK_APPS),$(eval $(call UK_APP_template,$(app))))
