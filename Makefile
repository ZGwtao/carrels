IMAGE_NAME ?= template-pd-manifest-env
IMAGE_TAG ?= local
IMAGE := $(IMAGE_NAME):$(IMAGE_TAG)

REMOTE_IMAGE ?= ghcr.io/zgwtao/template-pd-manifest-env:selective

CONTAINER_NAME ?= carrels-dev
WORKDIR ?= /workspace/carrels

CARRELS_DIR := $(realpath $(CURDIR))

DOCKER_RUN := docker run --rm -it \
	--workdir $(WORKDIR) \
	--mount type=bind,source=$(CARRELS_DIR),target=$(WORKDIR) \
	$(IMAGE)

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make init           Initialise all git submodules"
	@echo "  make image          Pull the environment image from GHCR"
	@echo "  make shell          Open an ephemeral development shell"
	@echo "  make dev            Open/reuse a persistent container for dev"
	@echo "  make check          Check the container environment"
	@echo "  make build          Build the simple example"
	@echo "  make qemu           Run the simple example under QEMU"
	@echo "  make qemu-clean     Clean-run the simple example under QEMU"
	@echo "  make dev-reset      Remove the persistent container"
	@echo "  make clean          Remove the build directories"

.PHONY: init
init:
	git submodule update --init --recursive


.PHONY: image
image:
	docker pull $(REMOTE_IMAGE)
	docker tag $(REMOTE_IMAGE) $(IMAGE)

.PHONY: shell
shell:
	$(DOCKER_RUN) /bin/bash

.PHONY: dev
dev:
	@if docker container inspect $(CONTAINER_NAME) >/dev/null 2>&1; then \
		docker start --attach --interactive $(CONTAINER_NAME); \
	else \
		docker run -it \
			--name $(CONTAINER_NAME) \
			--workdir $(WORKDIR) \
			--mount type=bind,source=$(CARRELS_DIR),target=$(WORKDIR) \
			$(IMAGE) \
			/bin/bash; \
	fi

.PHONY: check
check:
	docker run --rm \
		--workdir $(WORKDIR) \
		--mount type=bind,source=$(CARRELS_DIR),target=$(WORKDIR),readonly \
		$(IMAGE) \
		/bin/bash -c '\
			set -eu; \
			echo "CARRELS=$$(pwd)"; \
			echo "MICROKIT_SDK=$$MICROKIT_SDK"; \
			echo "LIONSOS=$$LIONSOS"; \
			test "$$(pwd)" = "$(WORKDIR)"; \
			test -f pc.mk; \
			test -x "$$MICROKIT_SDK/bin/microkit"; \
			test -d "$$LIONSOS"; \
			python -c "import sdfgen"; \
			echo "carrels environment check passed" \
		'

.PHONY: build
build:
	$(DOCKER_RUN) \
		/bin/bash -lc '$(MAKE) -C examples/simple'

.PHONY: dev-reset
dev-reset:
	@if docker container inspect $(CONTAINER_NAME) >/dev/null 2>&1; then \
		docker rm --force $(CONTAINER_NAME); \
	else \
		echo "Container $(CONTAINER_NAME) does not exist"; \
	fi

.PHONY: clean
clean:
	$(DOCKER_RUN) \
		/bin/bash -lc 'rm -rf examples/simple/build'

.PHONY: qemu
qemu:
	$(DOCKER_RUN) \
		/bin/bash -lc '$(MAKE) -C examples/simple qemu'

.PHONY: qemu-clean
qemu-clean:
	$(DOCKER_RUN) \
		/bin/bash -lc 'rm -rf examples/simple/build; $(MAKE) -C examples/simple qemu'
