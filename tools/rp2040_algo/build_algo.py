#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Сборка Thumb-блоба RP2040 и генерация rp2040_algo.h.

Блоб исполняется из SRAM RP2040 и вызывает ROM-функции bootrom
(connect_internal_flash / flash_exit_xip / flash_range_erase /
flash_range_program / flash_flush_cache / flash_enter_cmd_xip).

Запуск (вручную, при изменении rp2040_algo.s):

    python src/module_program/tools/rp2040_algo/build_algo.py

Требуется arm-none-eabi-as/ld/objcopy/nm (ищутся в PATH, затем в
%USERPROFILE%\\.pico-sdk\\toolchain\\*\\bin). Результат коммитится.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT_HEADER = HERE.parent.parent / "submodule_rpico" / "rp2040_algo.h"

BLOB_BASE = 0x20000200
ENTRIES = ("rp2040_algo_lookup", "rp2040_algo_erase", "rp2040_algo_prog")


def find_tool(name: str) -> str:
    exe = shutil.which(name)
    if exe:
        return exe
    home = Path(os.environ.get("USERPROFILE", str(Path.home())))
    toolchain_root = home / ".pico-sdk" / "toolchain"
    if toolchain_root.is_dir():
        for ver in sorted(toolchain_root.iterdir(), reverse=True):
            cand = ver / "bin" / f"{name}.exe"
            if cand.is_file():
                return str(cand)
    sys.exit(f"[algo] ERROR: {name} not found (PATH / ~/.pico-sdk/toolchain)")


def run(cmd):
    print("[algo] " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    out_dir = HERE / "out"
    out_dir.mkdir(exist_ok=True)

    src = HERE / "rp2040_algo.s"
    obj = out_dir / "rp2040_algo.o"
    elf = out_dir / "rp2040_algo.elf"
    bin_path = out_dir / "rp2040_algo.bin"

    run([find_tool("arm-none-eabi-as"), "-mcpu=cortex-m0plus", "-mthumb",
         "-o", obj, src])
    run([find_tool("arm-none-eabi-ld"), f"-Ttext=0x{BLOB_BASE:08x}",
         "--entry=rp2040_algo_lookup", "-o", elf, obj])
    run([find_tool("arm-none-eabi-objcopy"), "-O", "binary", elf, bin_path])

    nm = subprocess.run([find_tool("arm-none-eabi-nm"), str(elf)],
                        check=True, capture_output=True, text=True).stdout
    addrs = {}
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in ENTRIES:
            addrs[parts[2]] = int(parts[0], 16)
    missing = [e for e in ENTRIES if e not in addrs]
    if missing:
        sys.exit(f"[algo] ERROR: symbols not found: {missing}")

    blob = bin_path.read_bytes()
    if len(blob) > 512:
        sys.exit(f"[algo] ERROR: blob too big ({len(blob)} > 512 bytes)")

    lines = []
    lines.append("// Автогенерируемый файл — НЕ РЕДАКТИРОВАТЬ.")
    lines.append("// Источник: src/module_program/tools/rp2040_algo/rp2040_algo.s")
    lines.append("// Генератор: build_algo.py")
    lines.append("")
    lines.append("#ifndef _SUBMODULE_RPICO_ALGO_h")
    lines.append("#define _SUBMODULE_RPICO_ALGO_h")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define RP2040_ALGO_SIZE        {len(blob)}")
    for name in ENTRIES:
        macro = "RP2040_ALGO_" + name.replace("rp2040_algo_", "").upper() + "_OFS"
        lines.append(f"#define {macro:<23} 0x{addrs[name] - BLOB_BASE:03x}")
    lines.append("")
    lines.append(f"static const uint8_t rp2040_algo_blob[RP2040_ALGO_SIZE] = {{")
    for i in range(0, len(blob), 12):
        chunk = ", ".join(f"0x{b:02x}" for b in blob[i:i + 12])
        lines.append(f"\t{chunk},")
    lines.append("};")
    lines.append("")
    lines.append("#endif // _SUBMODULE_RPICO_ALGO_h")
    lines.append("")

    OUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    OUT_HEADER.write_text("\n".join(lines), encoding="utf-8")
    print(f"[algo] OK: {len(blob)} bytes -> {OUT_HEADER}")
    for name in ENTRIES:
        print(f"[algo]   {name} @ blob+0x{addrs[name] - BLOB_BASE:03x}")


if __name__ == "__main__":
    main()
