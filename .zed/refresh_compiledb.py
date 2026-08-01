#!/usr/bin/env python3

import json
import shutil
import subprocess
from pathlib import Path


project_dir = Path(__file__).resolve().parent.parent
database_path = project_dir / "compile_commands.json"
pio = shutil.which("pio") or str(Path.home() / ".platformio" / "penv" / "bin" / "pio")

subprocess.run(
    [pio, "run", "-e", "default", "-t", "compiledbtc"],
    cwd=project_dir,
    check=True,
)

with database_path.open(encoding="utf-8") as database_file:
    database = json.load(database_file)

toolchain_dir = Path.home() / ".platformio" / "packages" / "toolchain-riscv32-esp"
remove_flags = (
    f"-I{toolchain_dir / 'picolibc' / 'include'}",
    "-fstrict-volatile-bitfields",
    "-fno-tree-switch-conversion",
)

for entry in database:
    command = entry["command"]
    for flag in remove_flags:
        command = command.replace(f" {flag}", "")
    if " --target=riscv32-esp-elf " not in command:
        command = command.replace(" -o ", " --target=riscv32-esp-elf -o ", 1)
    entry["command"] = command

with database_path.open("w", encoding="utf-8") as database_file:
    json.dump(database, database_file, indent=2)
    database_file.write("\n")

print("Prepared compile_commands.json for Zed/clangd.")
