# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

PC_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))

include $(PC_SRC_DIR)/mk/libtrustedlo.mk
include $(PC_SRC_DIR)/mk/common.mk

include $(PC_SRC_DIR)/src/client/client.mk
include $(PC_SRC_DIR)/src/monitor/monitor.mk
include $(PC_SRC_DIR)/src/orchestrator/orchestrator.mk

PC_PROTOCON_OBJS :=
PC_TRAMPOLINE_OBJS :=

PC_OBJS := \
	$(PC_ORCHESTRATOR_OBJS) \
	$(PC_MONITOR_OBJS) \
	$(PC_PROTOCON_OBJS) \
	$(PC_TRAMPOLINE_OBJS) \
	$(PC_WHOAMI_CLIENT_OBJS) \
	$(PC_FS_CLIENT_OBJS)

-include $(PC_OBJS:.o=.d)


DOCKER_IMAGE ?= template-pd-manifest-env:local
CARRELS_DIR := $(CURDIR)

.PHONY: docker-env
docker-env:
	@docker image inspect "$(DOCKER_IMAGE)" >/dev/null 2>&1 || { \
		echo "Docker image not found: $(DOCKER_IMAGE)"; \
		echo "Build it from template-pd-manifest first."; \
		exit 1; \
	}

.PHONY: docker-run
docker-run: docker-env
	docker run --rm -it \
		--user "$$(id -u):$$(id -g)" \
		-e HOME=/tmp/carrels-home \
		-v "$(CARRELS_DIR):$(CARRELS_DIR)" \
		-w "$(CARRELS_DIR)" \
		"$(DOCKER_IMAGE)" \
		/bin/bash

.PHONY: docker-check
docker-check: docker-env
	docker run --rm \
		--user "$$(id -u):$$(id -g)" \
		-e HOME=/tmp/carrels-home \
		-v "$(CARRELS_DIR):$(CARRELS_DIR)" \
		-w "$(CARRELS_DIR)" \
		"$(DOCKER_IMAGE)" \
		/bin/bash -c '\
			set -eu; \
			echo "PWD=$$PWD"; \
			echo "MICROKIT_SDK=$$MICROKIT_SDK"; \
			python --version; \
			python -c "import sdfgen"; \
			python -c "from elftools.elf.elffile import ELFFile"; \
			command -v lex; \
			test -d "$$MICROKIT_SDK"; \
			echo "carrels environment check passed" \
		'

CLANG_TIDY ?= clang-tidy
TIDY_FILES := $(shell find $(PC_SRC_DIR)/src \
	-type f -name '*.c' -print)

.PHONY: tidy
tidy: $(PC_MONITOR_VM_LAYOUT_HEADER) pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(CLANG_TIDY) $(TIDY_FILES) -- $(CFLAGS) $(PC_CFLAGS)
