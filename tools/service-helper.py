
import sys
import tomllib
import argparse
from pathlib import Path
from elftools.elf.elffile import ELFFile
import struct

hdr = struct.Struct("<QII")
svc = struct.Struct("<iQQ")

MANIFEST_MAGIC = 0x504353564D414E31

service_types = {
    "dummy": -1,
    "file_system": 0,
    "device_serial": 1,
    "device_ethernet": 2,
    "device_timer": 3,
    "device_i2c": 4,
    "reserved": 5,
}

service_keys = {"type", "target"}


def validate_manifest(path: Path) -> list[dict]:
    with path.open("rb") as file:
        manifest = tomllib.load(file)
    if set(manifest) != {"service"}:
        raise ValueError("Top-level key must be 'service'")

    services = manifest["service"]
    if not services or not isinstance(services, list):
        raise ValueError("'service' must be a non-empty array")

    for idx, s in enumerate(services):
        if (
            not isinstance(s, dict)
            or set(s) != service_keys
            or s.get("type") not in service_types
            or not isinstance(s.get("target"), str)
            or not s["target"]
        ):
            raise ValueError(f"invalid service[{idx}]: {s}")

    return services


def resolve_service_offsets(
    elf_path: Path,
    services: list[dict],
) -> list[dict]:

    resolved = []
    with elf_path.open("rb") as file:
        elf = ELFFile(file)
        e_entry = elf.header["e_entry"]

        for idx, service in enumerate(services):
            target = service["target"]
            sec = elf.get_section_by_name(target)
            if sec is None:
                raise ValueError(
                    f"service[{idx}] target section not found: {target}"
                )
            sec_addr = sec.header["sh_addr"]
            sec_offset = sec_addr - e_entry
            resolved.append({
                # **service,
                "type": service_types[service["type"]],
                "addr": sec_addr,
                "offset": sec_offset,
                "size": sec.header["sh_size"],
            })
    return resolved


def serialize_services(services: list[dict]) -> bytes:
    data = bytearray()
    for s in services:
        type = s["type"]
        offset = s["offset"]
        size = s["size"]
        data.extend(svc.pack(type, offset, size))
    return bytes(data)


def serialize_manifest(services: list[dict]) -> bytes:
    data_services = serialize_services(services)
    total_size = hdr.size + len(data_services)
    data_header = hdr.pack(
        MANIFEST_MAGIC,
        len(services),
        total_size,
    )
    data_mf = data_header + data_services
    return data_mf


def image(
    path_elf: Path,
    path_out: Path,
    services: list[dict],
) -> None:
    data_mf = serialize_manifest(services)
    data_elf = path_elf.read_bytes()
    path_out.write_bytes(data_mf + data_elf)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", required=True)
    parser.add_argument("--mf", required=True)
    parser.add_argument("--elf", required=True)
    args = parser.parse_args()

    try:
        services = validate_manifest(path=Path(args.mf))
        services = resolve_service_offsets(Path(args.elf), services)
        image(
            path_elf=Path(args.elf),
            path_out=Path(args.o),
            services=services,
        )
    except (OSError, tomllib.TOMLDecodeError, ValueError, struct.error) as err:
        print(f"error: {err}", file=sys.stderr)
        raise SystemExit(1)
