#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import shutil
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MKVIRTDISK = REPO_ROOT / "dep/sddf/tools/mkvirtdisk"


@dataclass(frozen=True)
class PartitionDetails:
    start_sector: int
    sector_count: int
    sector_size: int

    @property
    def offset(self) -> int:
        return self.start_sector * self.sector_size

    @property
    def size(self) -> int:
        return self.sector_count * self.sector_size


def require_command(command: str) -> None:
    if shutil.which(command) is None:
        raise RuntimeError(f"required command not found: {command}")


def require_file(path: Path, description: str) -> None:
    if not path.is_file():
        raise RuntimeError(f"{description} does not exist: {path}")


def read_partition_table(disk: Path) -> dict:
    require_file(disk, "disk image")
    require_command("sfdisk")
    result = subprocess.run(
        ["sfdisk", "--json", str(disk)],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)["partitiontable"]


def partition_details(table: dict, number: int) -> PartitionDetails:
    partitions = table.get("partitions", [])
    if number < 1 or number > len(partitions):
        raise RuntimeError(f"partition {number} does not exist")

    partition = partitions[number - 1]
    return PartitionDetails(
        start_sector=int(partition["start"]),
        sector_count=int(partition["size"]),
        sector_size=int(table.get("sectorsize", 512)),
    )


def require_fat_partition(disk: Path, table: dict, number: int) -> None:
    require_command("blkid")
    # NOTE
    # blkid is a tool that validate a virt-disk drive as a whole by default.
    # If what needs to be validated is a specific partition, we need to
    # calculate the offset & size & range of a partition within a disk drive,
    # which explains why the "partition_details" must be invoked here.
    partition = partition_details(table, number)
    result = subprocess.run(
        [
            "blkid", "-p", "-o", "value", "-s", "TYPE",
            "-O", str(partition.offset), "-S", str(partition.size),
            str(disk),
        ],
        capture_output=True,
        text=True,
    )
    filesystem = result.stdout.strip().lower()
    # @gt ?? blkid returns vfat for FAT??
    if result.returncode != 0 or filesystem != "vfat":
        detected = filesystem or "unknown"
        raise RuntimeError(
            f"partition {number} uses {detected}; only FAT filesystems are supported"
        )


def cmd_create_virt_disk(args: argparse.Namespace) -> None:
    mkvirtdisk = args.mkvirtdisk.resolve()
    require_file(mkvirtdisk, "mkvirtdisk tool")
    args.disk.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(mkvirtdisk),
            str(args.disk),
            str(args.partitions),
            str(args.sector_size),
            str(args.disk_size),
            args.schema,
            args.filesystem,
        ],
        check=True,
    )


def cmd_copy_file(args: argparse.Namespace) -> None:
    require_file(args.file, "source file")
    require_command("mcopy")
    table = read_partition_table(args.disk)
    # Sanity check: FAT-only
    require_fat_partition(args.disk, table, args.partition)
    partition = partition_details(table, args.partition)
    subprocess.run(
        [
            "mcopy",
            "-o",
            "-i",
            f"{args.disk}@@{partition.offset}",
            str(args.file),
            f"::/{args.file.name}",
        ],
        check=True,
    )
    print(f"Copied {args.file.name} to partition {args.partition}")


def cmd_list_disk(args: argparse.Namespace) -> None:
    require_command("mdir")
    table = read_partition_table(args.disk)

    # When a specific partition is given, list the content within it,
    # otherwise list all contents of every partition in the virt-disk.
    if args.partition is None:
        partition_numbers = range(1, len(table.get("partitions", [])) + 1)
    else:
        partition_numbers = (args.partition,)

    # @gt ?? Currently this is FAT-only.
    for number in partition_numbers:
        require_fat_partition(args.disk, table, number)

    print(f"Disk image: {args.disk}\n", flush=True)
    print("Partition table:", flush=True)

    # NOTE
    # sfdisk (scriptable fdisk), which is a tool provided by "util-Linux"
    #
    subprocess.run(["sfdisk", "--list", str(args.disk)], check=True)

    print("\nFilesystem contents:", flush=True)

    for number in partition_numbers:
        partition = partition_details(table, number)
        print(
            f"\n{'=' * 60}\n"
            f"Partition {number}\n"
            f"Start sector : {partition.start_sector}\n"
            f"Sector count : {partition.sector_count}\n"
            f"Byte offset  : {partition.offset}\n"
            f"Size         : {partition.size} bytes\n"
            f"{'=' * 60}",
            flush=True,
        )
        # @gt ?? mdir (from mtools) is FAT-only. If ext4 is required,
        #        we should consider debugfs instead of mtools
        subprocess.run(
            ["mdir", "-i", f"{args.disk}@@{partition.offset}", "::/"],
            check=True,
        )


def positive_integer(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be a positive integer")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    # cmd1:
    #   virt_disk.py create VIRT_DISK --partitions COUNT --disk-size BYTES
    #       [--sector-size BYTES] [--schema {GPT,MBR}]
    #       [--filesystem {fat,ext4}] [--mkvirtdisk PATH]
    #
    create = subparsers.add_parser("create", help="create and format a disk image")
    create.add_argument("disk", type=Path)
    # @gt ?? We now assume all partitions are equal,
    #        so we only need to specify the partitions via (number + size)
    #        Should we allow arbitrart partition sizes?
    create.add_argument(
        "--partitions", type=positive_integer, required=True,
        help="number of equal-sized partitions",
    )
    create.add_argument(
        "--sector-size", type=positive_integer, default=512,
        help="logical sector size in bytes (default: 512)",
    )
    # NOTE
    # A virt-disk has multiple partitions, each of which has multiple logical sectors.
    # You may think of a virt-disk as a book, while a partition is a chapter and the
    # logical sectors are the pages within a chapter.
    create.add_argument(
        "--disk-size", type=positive_integer, required=True,
        help="total disk image size in bytes",
    )
    # NOTE
    # For each block disk drive, we need a schema as a way to organise the disk format
    # The schema standardises how each partition is recorded, while each partition will
    # have a way to organise the data content, so that's why we need a filesystem
    # within each partition.
    create.add_argument("--schema", choices=("GPT", "MBR"), default="GPT")
    create.add_argument("--filesystem", choices=("fat", "ext4"), default="fat")
    # NOTE
    # This script is provided by dep/sddf/tools
    create.add_argument("--mkvirtdisk", type=Path, default=DEFAULT_MKVIRTDISK)
    create.set_defaults(handler=cmd_create_virt_disk)

    # cmd2:
    #   virt_disk.py copy -f FILE -d VIRT_DISK -p PARTITION_ID
    #
    copy = subparsers.add_parser("copy", help="copy a file into a virt-disk at a partition")
    copy.add_argument("-f", "--file", type=Path, required=True)
    copy.add_argument("-d", "--disk", type=Path, required=True)
    copy.add_argument(
        "-p", "--partition", type=positive_integer, required=True,
        help="one-based (begins at '1') partition number",
    )
    copy.set_defaults(handler=cmd_copy_file)

    # cmd3:
    #   virt_disk.py list -d VIRT_DISK [-p PARTITION_ID]
    #
    list_parser = subparsers.add_parser("list", help="list partitions and files")
    list_parser.add_argument("-d", "--disk", type=Path, required=True)
    list_parser.add_argument(
        "-p", "--partition", type=positive_integer,
        help="one-based (begins at '1') partition number; omit to list every partition",
    )
    list_parser.set_defaults(handler=cmd_list_disk)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        args.handler(args)
    except (RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"virt_disk.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
