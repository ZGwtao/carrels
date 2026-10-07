# Deploying a Carrels application

`c-hello.yaml` is the minimal Pod manifest for the current PoC:

```sh
kubectl apply -f examples/simple/k8s/c-hello.yaml
kubectl get pod carrels-c-hello -w
```

Delete the Pod to stop its Carrels container:

```sh
kubectl delete -f examples/simple/k8s/c-hello.yaml
```

Applying the same file twice updates the same Pod; it does not create a second
one. Give each Pod a unique `metadata.name` to create multiple instances:

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: carrels-c-hello-2
spec:
  nodeName: sel4-worker
  automountServiceAccountToken: false
  restartPolicy: Never
  containers:
    - name: c-hello
      image: carrels.local/c-hello:latest
      imagePullPolicy: IfNotPresent
```

Keep `nodeName: sel4-worker`: it schedules the Pod onto the kubelet inside the
VM. Use `IfNotPresent` because the PoC does not pull from a registry. The host
backend accepts an image only when its corresponding `.img` file was present on
the QEMU disk when the filesystem was mounted.

## Adding another application

For an application named `my-app`:

1. Build its Unikraft image as `unikraft-my-app.img`, for example with
   `./build-app.sh unikraft-my-app`.
2. Add `my-app.img` to `K8S_APPLICATION_IMAGES` in `container.mk`, with a rule
   that copies it from `unikraft-my-app.img`.
3. Extend the image mapping in `src/orchestrator/orchestrator.c` so
   `carrels.local/my-app:latest` maps to `my-app.img`.
4. Rebuild with `./build-infra.sh` and `./build-ramdisk.sh`.
5. Copy `c-hello.yaml`, choose a unique `metadata.name`, and change the
   container name and image:

```yaml
containers:
  - name: my-app
    image: carrels.local/my-app:latest
    imagePullPolicy: IfNotPresent
```

Commands, arguments, environment variables, volumes, and registry image pulls
are not implemented by this PoC. Application behaviour is compiled into the
Unikraft image.
