#!/usr/bin/env python3

# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import argparse
from pathlib import Path


def pad_payload(source: Path, output: Path, size: int) -> None:
    payload = source.read_bytes()
    if len(payload) > size:
        raise ValueError(
            f"{source} is {len(payload)} bytes and does not fit in {size} bytes"
        )

    with output.open("wb") as destination:
        destination.write(payload)
        destination.truncate(size)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Pad a valid ELF payload to an exact file size"
    )
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("size", type=int)
    args = parser.parse_args()

    try:
        pad_payload(args.source, args.output, args.size)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
