# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

from pathlib import Path
import importlib.util


class VSpace:
    def __init__(self, regions: dict[str, dict[str, int]]):
        self.regions = regions

    @classmethod
    def load(cls, path: str, module_name: str) -> "VSpace":
        layout_path = Path(path).resolve()

        if not layout_path.is_file():
            raise FileNotFoundError(
                f"VM layout does not exist: {layout_path}"
            )

        spec = importlib.util.spec_from_file_location(
            module_name,
            layout_path,
        )

        if spec is None or spec.loader is None:
            raise RuntimeError(
                f"Cannot load VM layout: {layout_path}"
            )

        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        regions = getattr(module, "VM_REGIONS", None)

        if not isinstance(regions, list):
            raise TypeError(
                "VM layout must define VM_REGIONS as a list"
            )

        result: dict[str, dict[str, int]] = {}

        for index, region in enumerate(regions):
            if not isinstance(region, dict):
                raise TypeError(
                    f"VM_REGIONS[{index}] must be a dictionary"
                )

            try:
                name = region["name"]
                base = region["base"]
                size = region["size"]
            except KeyError as error:
                raise ValueError(
                    f"VM_REGIONS[{index}] is missing "
                    f"{error.args[0]!r}"
                ) from error

            if (
                not isinstance(name, str)
                or not isinstance(base, int)
                or not isinstance(size, int)
            ):
                raise TypeError(
                    f"VM_REGIONS[{index}] must contain "
                    "string name and integer base/size"
                )

            if name in result:
                raise ValueError(
                    f"Duplicate VM region: {name}"
                )

            result[name] = {
                "base": base,
                "size": size,
            }

        return cls(result)

    def region(self, name: str) -> dict[str, int]:
        try:
            return self.regions[name]
        except KeyError as error:
            raise KeyError(
                f"Required VM region is missing: {name}"
            ) from error

    def base(self, name: str) -> int:
        return self.region(name)["base"]

    def size(self, name: str) -> int:
        return self.region(name)["size"]

    def region_base(self, name: str, index: int) -> int:
        region = self.region(name)
        return region["base"] + index * region["size"]