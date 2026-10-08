#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

from pathlib import Path
import subprocess
import sys


STATIC_COPY_TABLE = [
    ("protocon.elf", 1),
    ("trampoline.elf", 1),
    ("container_monitor.svc", 2),
    ("container_monitor.dlg", 2),
]

def main() -> int:
    build_dir = Path(sys.argv[1]).resolve()
    protocon_count = int(sys.argv[2])
    disk_tool = Path(__file__).resolve().parents[2] / "tools/virt_disk.py"
    disk = build_dir / "qemu_disk"

    application_images = [
        f for f in sorted(build_dir.glob("*.img"))
        if f.name != "container.img"
    ]
    copy_table = STATIC_COPY_TABLE + \
        [(f, 2) for f in sorted(build_dir.glob("*.data"))] + \
        [(f, 2) for f in sorted(build_dir.glob("symbols/*.mktsym"))] + \
        [(f, 1) for f in application_images]

    for x in range(2, 2 + protocon_count):
        copy_table += [(f, x) for f in sorted(build_dir.glob("disk-test.txt"))]

    for source, partition in copy_table:
        print(f"Copying {source} to partition {partition}")
        result = subprocess.run(
            [sys.executable, str(disk_tool), "copy",
             "--file", str(source), "--disk", str(disk),
             "--partition", str(partition)]
        )
        if result.returncode: return result.returncode

    print("All files copied successfully.")
    return subprocess.run(
        [sys.executable, str(disk_tool), "list", "--disk", str(disk)]
    ).returncode


if __name__ == "__main__":
    sys.exit(main())
