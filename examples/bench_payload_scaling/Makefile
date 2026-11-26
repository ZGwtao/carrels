# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

override MICROKIT_SDK := $(abspath $(MICROKIT_SDK))
export CARRELS ?= $(abspath ../..)
export ROOT ?= $(CARRELS)
BUILD_DEFAULTS := $(CARRELS)/config/build-defaults.sh
DEFAULT_MICROKIT_BOARD := $(shell . $(BUILD_DEFAULTS); printf '%s' "$$MICROKIT_BOARD")
DEFAULT_MICROKIT_CONFIG := smp-benchmark
export MICROKIT_BOARD ?= $(DEFAULT_MICROKIT_BOARD)
export MICROKIT_CONFIG ?= $(DEFAULT_MICROKIT_CONFIG)
export BENCH_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ifeq ($(MICROKIT_BOARD),x86_64_generic)
X86_BOARD ?= qemu_virt_x86
export BUILD_DIR ?= $(BENCH_DIR)/build/$(MICROKIT_BOARD)/$(X86_BOARD)/$(MICROKIT_CONFIG)
else
export BUILD_DIR ?= $(BENCH_DIR)/build/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
endif

IMAGE_FILE := $(BUILD_DIR)/bench_payload_scaling.img
REPORT_FILE := $(BUILD_DIR)/report.txt

.PHONY: all qemu clean clobber FORCE
all qemu clean clobber: $(BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(BUILD_DIR) MICROKIT_SDK=$(MICROKIT_SDK) $@

$(IMAGE_FILE) $(REPORT_FILE): $(BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(BUILD_DIR) MICROKIT_SDK=$(MICROKIT_SDK) $(notdir $@)

$(BUILD_DIR)/Makefile: payload_scaling.mk Makefile
	mkdir -p $(BUILD_DIR)
	echo "ROOT := $(ROOT)" > $@
	echo "CARRELS := $(CARRELS)" >> $@
	echo "BENCH_DIR := $(BENCH_DIR)" >> $@
	echo "BUILD_DIR := $(BUILD_DIR)" >> $@
	echo "MICROKIT_BOARD := $(MICROKIT_BOARD)" >> $@
	echo "MICROKIT_CONFIG := $(MICROKIT_CONFIG)" >> $@
	echo "X86_BOARD := $(X86_BOARD)" >> $@
	cat payload_scaling.mk >> $@

FORCE:
