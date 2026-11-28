# SPDX-FileCopyrightText: 2026 UNSW
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import struct
from pathlib import Path


MANIFEST_HEADER = struct.Struct("<QIIQ")
ELF_SECTION_OFFSET = 40
PAYLOAD_CAPACITY = 0x800000


def write_variant(output_dir: Path, name: str, image: bytearray) -> None:
    (output_dir / name).write_bytes(image)


def generate(source: Path, output_dir: Path) -> None:
    original = bytearray(source.read_bytes())
    _, _, manifest_size, _ = MANIFEST_HEADER.unpack_from(original)

    image = original.copy()
    struct.pack_into("<Q", image, 0, 0)
    write_variant(output_dir, "bad-manifest-magic.img", image)

    image = original.copy()
    struct.pack_into("<Q", image, 16, PAYLOAD_CAPACITY)
    write_variant(output_dir, "oversized-payload.img", image)

    image = original.copy()
    image[manifest_size : manifest_size + 4] = b"BAD!"
    write_variant(output_dir, "bad-elf-magic.img", image)

    image = original.copy()
    struct.pack_into("<Q", image, manifest_size + ELF_SECTION_OFFSET, PAYLOAD_CAPACITY)
    write_variant(output_dir, "bad-section-table.img", image)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    generate(args.source, args.output_dir)


if __name__ == "__main__":
    main()
