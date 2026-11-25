#!/usr/bin/env python3

import argparse
import shutil
from pathlib import Path


def copy_files(source: Path, destination: Path, patterns: tuple[str, ...]) -> None:
    for pattern in patterns:
        files = sorted(path for path in source.glob(pattern)
                       if path.is_file() and not path.name.startswith("container."))
        if not files:
            raise SystemExit(f"RAMFS initrd: no files matching {pattern!r} in {source}")

        for path in files:
            # The monitor opens mktsym files by basename, matching the FAT
            # disk copier's layout rather than the build output directory.
            target = destination / (path.name if pattern.startswith("symbols/")
                                    else path.relative_to(source))
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("role", choices=("monitor", "orchestrator"))
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()

    if args.destination.exists():
        shutil.rmtree(args.destination)

    args.destination.mkdir(parents=True)

    if args.role == "monitor":
        copy_files(args.source, args.destination,
                   ("container_monitor.svc", "container_monitor.dlg", "*.data",
                    "symbols/*.mktsym"))
    else:
        copy_files(args.source, args.destination,
                   ("protocon.elf", "trampoline.elf"))

        for filename in args.files:

            source = args.source / filename

            if not source.is_file():
                raise SystemExit(f"RAMFS initrd: missing application {source}")

            shutil.copy2(source, args.destination / source.name)


if __name__ == "__main__":
    main()
