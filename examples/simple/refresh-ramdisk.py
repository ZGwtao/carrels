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
    ("build/delegation/container_monitor.dlg", 2),
]

def main() -> int:
    build_dir = Path(sys.argv[1]).resolve()
    script_dir = Path(__file__).resolve().parent
    copy_script = script_dir / "copy2ramdisk.sh"

    copy_table = STATIC_COPY_TABLE + \
        [(f, 2) for f in sorted(build_dir.glob("*.data"))] + \
        [(f, 2) for f in sorted(build_dir.glob("symbols/*.mktsym"))] + \
        [(f, 1) for f in sorted(build_dir.glob("*.img"))]

    for source, partition in copy_table:
        print(f"Copying {source} to partition {partition}")
        result = subprocess.run([str(copy_script), str(source), str(partition)])
        if result.returncode: return result.returncode

    print("All files copied successfully.")
    return subprocess.run([str(script_dir / "listramdisk.sh")]).returncode


if __name__ == "__main__":
    sys.exit(main())
