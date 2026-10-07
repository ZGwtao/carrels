<!--
SPDX-FileCopyrightText: 2026 UNSW

SPDX-License-Identifier: BSD-2-Clause
-->

# Carrels: Lightweight Sandbox on LionsOS & seL4

## Build

We provide a root Makefile for using Docker-based environment, which is pre-built from [template-PD-manifest](https://github.com/ZGwtao/template-pd-manifest). Developers can use instructions like `make image` to setup the develop environemnt, as it pulls the Docker image from [ghcr.io](https://github.com/zgwtao/template-pd-manifest/pkgs/container/template-pd-manifest-env). 

Other make targets are visible via `make help`, which prints out all available build instructions from the root Makefile. For instances, `make build` can help testing building the Carrels monitor and serveral given applications. By running `make qemu`, you can use the demo built for *QEMU* (Aarch64 by default), which interacts with the users via a `microrl` shell. 


For detail information, see [carrels-docs](https://github.com/ZGwtao/carrels-docs).

The direct Nix-based workflow targets `qemu_virt_aarch64` with the
`smp-debug` configuration by default:

```sh
./build_sdk.sh
./build-infra.sh
./build-ramdisk.sh
./qemu.sh
```

`./run.sh` performs a clean infrastructure/application/ramdisk build and then
starts QEMU. To build one application incrementally, use
`./build-app.sh <application-name>`. The defaults can be overridden with
`MICROKIT_BOARD`, `MICROKIT_CONFIG`, and `MICROKIT_SDK` where supported.
The shared board/config defaults live in `config/build-defaults.sh`.
Build artifacts are isolated under
`examples/simple/build/<board>/<configuration>/`, so ARM and x86 builds can
coexist.
