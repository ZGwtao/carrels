# End-to-end Kubernetes deployment

This guide builds the default `qemu_virt_aarch64`/`smp-debug` system, joins the
Linux guest running in QEMU to a local kind cluster as `sel4-worker`, and
deploys the `c-hello` Carrels image through Kubernetes.

Run all commands from the repository root.

## Host prerequisites

Install Nix with flakes enabled, Docker, kind, and kubectl. Docker must be
running and usable by the current user. The Nix development shell supplies the
cross compiler, QEMU, `sdfgen`, Python dependencies, and filesystem tools used
by the build.

Initialize the source dependencies and build the Microkit SDK once:

```sh
./build_sdk.sh
```

The default SDK is generated at
`dep/microkit/release/microkit-sdk-2.3.0-dev`.

## 1. Create the kind control plane

The QEMU user network exposes the host as `10.0.2.2`. The guest kubelet is
configured to contact the Kubernetes API at `https://10.0.2.2:6443`, so the
kind API server must listen on host port 6443.

```sh
kind create cluster --name libvmm-poc --config=- <<'EOF'
kind: Cluster
apiVersion: kind.x-k8s.io/v1alpha4
networking:
  apiServerAddress: "0.0.0.0"
  apiServerPort: 6443
nodes:
  - role: control-plane
EOF

kubectl --context kind-libvmm-poc cluster-info
docker ps --filter name=libvmm-poc-control-plane
```

If a cluster with that name already exists, do not recreate it. Confirm that
its API server is published on port 6443 with `docker port
libvmm-poc-control-plane`.

## 2. Generate the guest kubelet credential

Issue a least-privilege node credential from the running kind control plane:

```sh
make -C guest/k8s kubelet-credentials
test -s guest/k8s/credentials/kubelet.conf
```

The generated credential identifies the guest as
`system:node:sel4-worker`. It contains a private key, is ignored by Git, and
should not be committed. See
`guest/k8s/KUBELET_CREDENTIALS_TUTORIAL.md` for rotation and override options.

## 3. Build the K8s guest and Carrels images

The first guest build downloads and verifies the pinned Linux, BusyBox, Go,
and Kubernetes sources. Subsequent builds reuse `external/` caches.

```sh
make -C guest/k8s
./build-infra.sh
./build-app.sh unikraft-c-hello
./build-ramdisk.sh
```

The important outputs are:

```text
guest/k8s/build/linux/arch/arm64/boot/Image
guest/k8s/build/rootfs.cpio.gz
examples/simple/build/qemu_virt_aarch64/smp-debug/container.img
examples/simple/build/qemu_virt_aarch64/smp-debug/unikraft-c-hello.img
examples/simple/build/qemu_virt_aarch64/smp-debug/qemu_disk
```

`build-ramdisk.sh` copies the application as `c-hello.img` onto the QEMU disk.
The disk must be rebuilt, or updated with the command printed by
`build-app.sh`, whenever an application image changes. Do this while QEMU is
stopped.

## 4. Start QEMU and wait for the worker

Start QEMU in one terminal:

```sh
./qemu.sh
```

Successful boot output includes:

```text
k8s_vmm is ready
[@vsock_backend] CRI runtime listening on CID 2 port 1234
[@vsock_backend] CRI shim connected
```

In another terminal, wait for the guest kubelet to register:

```sh
kubectl --context kind-libvmm-poc wait \
  --for=condition=Ready node/sel4-worker --timeout=180s

kubectl --context kind-libvmm-poc get nodes -o wide
```

Use `Ctrl-A X` in the QEMU terminal to stop QEMU.

## 5. Deploy the Carrels application through Kubernetes

The supplied manifest pins the Pod to `sel4-worker` and uses the image name
recognized by the Carrels runtime:

```sh
kubectl --context kind-libvmm-poc apply \
  -f examples/simple/k8s/c-hello.yaml

kubectl --context kind-libvmm-poc get pod carrels-c-hello -w
```

In the QEMU console, a successful deployment includes output similar to:

```text
Deployed c-hello.img on protocon 0
[*] dynamic-PD [id=0] has state: in-use
Hello from Unikraft!
```

Inspect the Kubernetes side with:

```sh
kubectl --context kind-libvmm-poc get pod carrels-c-hello -o wide
kubectl --context kind-libvmm-poc describe pod carrels-c-hello
```

Delete the workload with:

```sh
kubectl --context kind-libvmm-poc delete \
  -f examples/simple/k8s/c-hello.yaml
```

## Current PoC limitations

- Only explicitly mapped Carrels images are available. The runtime does not
  pull arbitrary OCI images from a registry. Use `imagePullPolicy:
  IfNotPresent` and keep `nodeName: sel4-worker` in Carrels Pod manifests.
- The kind-managed `kube-proxy` and `kindnet` Pods scheduled onto
  `sel4-worker` may show `ErrImagePull`; their OCI images are not present in
  the Carrels image catalogue. This does not prevent a node-pinned `c-hello`
  image from being handed to the Carrels runtime.
- The current CRI/vsock proof of concept can deploy and execute the protection
  domain while Kubernetes remains at `ContainerCreating` if the
  `StartContainer` reply is not observed by kubelet. Confirm actual execution
  using the QEMU messages shown above.
- Commands, arguments, environment variables, volumes, registry pulls, and
  normal container networking semantics are not yet implemented. Application
  behaviour is compiled into the Unikraft image.

## Rebuilding after source changes

For an infrastructure-only change:

```sh
./build-infra.sh
```

For a `c-hello` application change:

```sh
./build-app.sh unikraft-c-hello
./build-ramdisk.sh
```

For guest kernel, kubelet, CRI shim, or initramfs changes:

```sh
make -C guest/k8s
./build-infra.sh
```

Stop QEMU before replacing `container.img` or `qemu_disk`, then start it again
with `./qemu.sh`.
