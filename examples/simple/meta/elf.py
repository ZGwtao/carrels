# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import os
import shutil
import subprocess

class elftools:
    def __init__(self, objcopy: str):
        self.objcopy = objcopy

    # Adds ".elf" to elf strings
    def copy_elf(self, source_elf: str, new_elf: str, elf_number=None):
        source_elf += ".elf"
        if elf_number != None:
            new_elf += str(elf_number)
        new_elf += ".elf"
        assert os.path.isfile(source_elf)
        return shutil.copyfile(source_elf, new_elf)


    # Assumes elf string has ".elf" suffix, and ".data" to data string
    def update_elf_section(
        self, elf_name: str, section_name: str, data_name: str, data_number=None
    ):
        assert os.path.isfile(elf_name)
        if data_number != None:
            data_name += str(data_number)
        data_name += ".data"
        assert os.path.isfile(data_name)
        assert (
            subprocess.run(
                [
                    self.objcopy,
                    "--update-section",
                    "." + section_name + "=" + data_name,
                    elf_name,
                ]
            ).returncode
            == 0
        )