#!/usr/bin/env python3

from pathlib import Path

import subprocess
import sys


STATIC_COPY_TABLE = [
    ("trampoline.elf", 1),
    ("protocon.elf", 1),
    ("client_timeout.img", 1),
    ("client_faulting.img", 1),
    ("client_echo.img", 1),
    ("client_looping.img", 1),
    ("bench_simple.img", 1),
    ("unikraft.img", 1),
]


def main() -> int:
    if len(sys.argv) != 2:
        print(
            f"Usage: {Path(sys.argv[0]).name} <build-dir>",
            file=sys.stderr,
        )
        return 1

    build_dir = Path(sys.argv[1]).resolve()
    if not build_dir.is_dir():
        print(
            f"Error: build directory does not exist: {build_dir}",
            file=sys.stderr,
        )
        return 1

    script_dir = Path(__file__).resolve().parent
    copy_script = script_dir / "copy2ramdisk.sh"

    if not copy_script.is_file():
        print(f"Error: cannot find {copy_script}", file=sys.stderr)
        return 1

    data_copy_table = [
        (data_file, 2)
        for data_file in sorted(build_dir.glob("*.data"))
    ]

    copy_table = STATIC_COPY_TABLE + data_copy_table

    for file_path, partition in copy_table:
        source = Path(file_path)

        if not source.is_file():
            print(
                f"Error: source file does not exist: {source}",
                file=sys.stderr,
            )
            return 1

        print(f"Copying {source} to partition {partition}")

        try:
            subprocess.run(
                [str(copy_script), str(source), str(partition)],
                check=True,
            )
        except subprocess.CalledProcessError as error:
            print(
                f"Error: failed to copy {source} "
                f"to partition {partition}, exit code {error.returncode}",
                file=sys.stderr,
            )
            return error.returncode

    print("All files copied successfully.")

    subprocess.run(
        [str(script_dir / "listramdisk.sh")],
        check=True,
    )

    return 0


if __name__ == "__main__":
    sys.exit(main())