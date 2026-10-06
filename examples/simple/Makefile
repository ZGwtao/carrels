#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})
export CARRELS ?= $(abspath ../..)
export ROOT ?= $(abspath ../..)
BUILD_DEFAULTS := $(CARRELS)/config/build-defaults.sh
DEFAULT_MICROKIT_BOARD := $(shell . $(BUILD_DEFAULTS); printf '%s' "$$MICROKIT_BOARD")
DEFAULT_MICROKIT_CONFIG := $(shell . $(BUILD_DEFAULTS); printf '%s' "$$MICROKIT_CONFIG")
export CONTAINER_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export CONFIG_INCLUDE ?= $(abspath src/config)
export MICROKIT_CONFIG ?= $(DEFAULT_MICROKIT_CONFIG)
export MICROKIT_BOARD ?= $(DEFAULT_MICROKIT_BOARD)
export BUILD_DIR ?= $(abspath build/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG))

IMAGE_FILE := $(BUILD_DIR)/container.img
REPORT_FILE := $(BUILD_DIR)/report.txt

.PHONY: all build infra apps app-native app-uk ramdisk qemu

all build infra apps app-native app-uk ramdisk qemu: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${IMAGE_FILE} ${REPORT_FILE} system clean qemu_disk refresh-ramdisk clobber tidy: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: container.mk Makefile $(BUILD_DEFAULTS) $(ROOT)/uk-on-mk.mk
	mkdir -p ${BUILD_DIR}
	echo "ROOT ?= ${ROOT}" > $@
	echo "CARRELS ?= ${CARRELS}" >> $@
	echo "export ROOT CARRELS" >> $@
	echo "export CONTAINER_DIR ?= ${CONTAINER_DIR}" >> $@
	echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" >> $@
	cat container.mk >> $@

submodules:
	cd $(CARRELS) && git submodule update --init --recursive
	@test ! -e $(CARRELS)/dep/uk-on-mk/dep/sddf || test -L $(CARRELS)/dep/uk-on-mk/dep/sddf || { echo "refusing to replace non-symlink $(CARRELS)/dep/uk-on-mk/dep/sddf" >&2; exit 1; }
	ln -sfn ../../sddf $(CARRELS)/dep/uk-on-mk/dep/sddf

FORCE:
