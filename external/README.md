<!--
SPDX-FileCopyrightText: 2026 UNSW
SPDX-License-Identifier: BSD-2-Clause
-->

# External sources and toolchains

This directory owns the pinned third-party inputs used by the Kubernetes guest:

- Linux 6.12
- BusyBox 1.37.0
- Go 1.26.0 (official Linux/amd64 toolchain)
- Kubernetes v1.37.0

Download and extract all sources and the Go toolchain with:

```sh
make -C external sources
```

Build the arm64 kubelet and CRI stub with:

```sh
make -C external
```

Archives are cached in `external/downloads`, source trees are extracted into
`external/src`, the Go toolchain lives in `external/toolchains`, and binaries
are written to `external/build`. These generated directories are ignored by
Git. Downloads are accepted only when their pinned SHA-256 checksum matches.

The downloaded amd64 Go toolchain cross-compiles pure-Go programs for arm64 by
setting `GOOS=linux`, `GOARCH=arm64`, and `CGO_ENABLED=0`. A separate arm64 Go
compiler is therefore unnecessary. Code that enables CGO would additionally
need an arm64 C cross-compiler and matching target libraries.
