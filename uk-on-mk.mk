# SPDX-FileCopyrightText: 2026 UNSW
# SPDX-License-Identifier: BSD-2-Clause

UK_ON_MK_DIR ?= $(ROOT)/dep/uk-on-mk
UK_APPS := sqlite nginx
UK_SERVICE_MANIFEST ?= $(ROOT)/src/client/uk.mf
UK_SERVICE_HELPER ?= $(ROOT)/tools/service-helper.py
UNIKERNELS := $(addprefix unikraft-,$(addsuffix .img,$(UK_APPS)))

define UK_APP_template

UK_APP_BUILD_DIR_$(1) := $(BUILD_DIR)/uk/$(1)
UK_APP_ELF_$(1) := $$(UK_APP_BUILD_DIR_$(1))/$(1)_default-arm64

.PHONY: uk-build-$(1)

uk-build-$(1): libsddf_util.a
	$$(MAKE) -f $(UK_ON_MK_DIR)/uk.mk \
		ROOT=$(UK_ON_MK_DIR) BUILD_DIR=$(BUILD_DIR) SDDF=$(SDDF) \
		MICROKIT_SDK=$(MICROKIT_SDK) MICROKIT_BOARD=$(MICROKIT_BOARD) \
		MICROKIT_CONFIG=$(MICROKIT_CONFIG) BOARD_DIR=$(BOARD_DIR) \
		BM_UK_APPLICATION=$(1) uk-build
	cp $$(UK_APP_ELF_$(1)) unikraft-$(1).elf

unikraft-$(1).elf: uk-build-$(1)

unikraft-$(1).img: unikraft-$(1).elf $(UK_SERVICE_MANIFEST) $(UK_SERVICE_HELPER)
	PYTHONPATH=$(SDDF)/tools/meta:$$$$PYTHONPATH $(PYTHON) $(UK_SERVICE_HELPER) \
		--mf $(UK_SERVICE_MANIFEST) --elf $$< -o $$@

endef

$(foreach app,$(UK_APPS),$(eval $(call UK_APP_template,$(app))))
