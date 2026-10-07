# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

EXTERNAL_DIR := $(CURDIR)
include versions.mk

CARRELS := $(abspath ..)
CRI_STUB_SOURCE := $(CARRELS)/guest/k8s/cri_stub.go
GO_STAMP := $(GO_ROOT)/.installed
LINUX_STAMP := $(LINUX_SRC)/.extracted
BUSYBOX_STAMP := $(BUSYBOX_SRC)/.extracted
KUBERNETES_STAMP := $(KUBERNETES_SRC)/.extracted
KUBERNETES_SEMVER := $(patsubst v%,%,$(KUBERNETES_VERSION))
KUBERNETES_MAJOR := $(word 1,$(subst ., ,$(KUBERNETES_SEMVER)))
KUBERNETES_MINOR := $(word 2,$(subst ., ,$(KUBERNETES_SEMVER)))

.PHONY: all sources linux busybox go kubernetes kubelet cri-stub clean

all: sources kubelet cri-stub

sources: linux busybox go kubernetes
linux: $(LINUX_STAMP)
busybox: $(BUSYBOX_STAMP)
go: $(GO_STAMP)
kubernetes: $(KUBERNETES_STAMP)
kubelet: $(KUBELET)
cri-stub: $(CRI_STUB)

$(LINUX_ARCHIVE):
	mkdir -p $(dir $@)
	curl --fail --location --retry 3 $(LINUX_URL) -o $@.tmp
	echo "$(LINUX_SHA256)  $@.tmp" | sha256sum --check -
	mv $@.tmp $@

$(LINUX_STAMP): $(LINUX_ARCHIVE)
	rm -rf $(LINUX_SRC)
	mkdir -p $(EXTERNAL_SOURCES)
	tar -C $(EXTERNAL_SOURCES) -xf $<
	test -f $(LINUX_SRC)/Makefile
	touch $@

$(BUSYBOX_ARCHIVE):
	mkdir -p $(dir $@)
	curl --fail --location --retry 3 $(BUSYBOX_URL) -o $@.tmp
	echo "$(BUSYBOX_SHA256)  $@.tmp" | sha256sum --check -
	mv $@.tmp $@

$(BUSYBOX_STAMP): $(BUSYBOX_ARCHIVE)
	rm -rf $(BUSYBOX_SRC)
	mkdir -p $(EXTERNAL_SOURCES)
	tar -C $(EXTERNAL_SOURCES) -xf $<
	test -f $(BUSYBOX_SRC)/Makefile
	touch $@

$(GO_ARCHIVE):
	mkdir -p $(dir $@)
	curl --fail --location --retry 3 $(GO_URL) -o $@.tmp
	echo "$(GO_SHA256)  $@.tmp" | sha256sum --check -
	mv $@.tmp $@

$(GO_STAMP): $(GO_ARCHIVE)
	rm -rf $(GO_ROOT)
	mkdir -p $(EXTERNAL_TOOLCHAINS)
	tar -C $(EXTERNAL_TOOLCHAINS) -xzf $<
	test "$$($(GO) version)" = "go version go$(GO_VERSION) linux/amd64"
	touch $@

$(KUBERNETES_ARCHIVE):
	mkdir -p $(dir $@)
	curl --fail --location --retry 3 $(KUBERNETES_URL) -o $@.tmp
	echo "$(KUBERNETES_SHA256)  $@.tmp" | sha256sum --check -
	mv $@.tmp $@

$(KUBERNETES_STAMP): $(KUBERNETES_ARCHIVE)
	rm -rf $(KUBERNETES_SRC)
	mkdir -p $(KUBERNETES_SRC)
	tar -C $(KUBERNETES_SRC) -xzf $<
	test -f $(KUBERNETES_SRC)/go.mod
	touch $@

$(KUBELET): $(GO_STAMP) $(KUBERNETES_STAMP) Makefile versions.mk
	mkdir -p $(EXTERNAL_BUILD) $(EXTERNAL_BUILD)/go-cache $(EXTERNAL_BUILD)/go-mod-cache
	cd $(KUBERNETES_SRC) && \
		PATH=$(GO_ROOT)/bin:$$PATH \
		GOCACHE=$(EXTERNAL_BUILD)/go-cache GOMODCACHE=$(EXTERNAL_BUILD)/go-mod-cache \
		GOOS=linux GOARCH=arm64 CGO_ENABLED=0 GOTOOLCHAIN=local \
		$(GO) build -mod=vendor -trimpath \
		-ldflags='-s -w -X k8s.io/component-base/version.gitVersion=$(KUBERNETES_VERSION) -X k8s.io/component-base/version.gitMajor=$(KUBERNETES_MAJOR) -X k8s.io/component-base/version.gitMinor=$(KUBERNETES_MINOR)' \
		-o $@ ./cmd/kubelet
	file $@

$(CRI_STUB): $(GO_STAMP) $(KUBERNETES_STAMP) $(CRI_STUB_SOURCE) Makefile versions.mk
	mkdir -p $(KUBERNETES_SRC)/_output/carrels-cri-stub $(EXTERNAL_BUILD)/go-cache $(EXTERNAL_BUILD)/go-mod-cache
	cp $(CRI_STUB_SOURCE) $(KUBERNETES_SRC)/_output/carrels-cri-stub/main.go
	cd $(KUBERNETES_SRC) && \
		PATH=$(GO_ROOT)/bin:$$PATH \
		GOCACHE=$(EXTERNAL_BUILD)/go-cache GOMODCACHE=$(EXTERNAL_BUILD)/go-mod-cache \
		GOOS=linux GOARCH=arm64 CGO_ENABLED=0 GOTOOLCHAIN=local \
		$(GO) build -mod=vendor -trimpath -ldflags='-s -w' \
		-o $@ ./_output/carrels-cri-stub
	file $@

clean:
	rm -rf $(EXTERNAL_BUILD)
