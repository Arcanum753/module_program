#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Сборка тестовых прошивок RP2040 (мигание LED 100/500/1000/2000 мс).

Запуск:

    python src/module_program/tools/rpico_test_fw/build_fw.py

Использует pico-sdk, ARM-тулчейн и picotool из установки Raspberry Pi Pico
VS Code extension (пути можно переопределить переменными окружения
PICO_SDK_PATH / PICO_TOOLCHAIN_PATH). Результат — 4 .bin в <repo>/hex_bin/.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD_DIR = HERE / "build"

# hex_bin/ в корне репозитория ядра (src/module_program/tools/rpico_test_fw -> 4 уровня вверх)
ROOT = HERE.parents[3]
OUT_DIR = ROOT / "hex_bin"

TARGETS = {
    100:  "pico_100ms.bin",
    500:  "pico_500ms.bin",
    1000: "pico_1s.bin",
    2000: "pico_2s.bin",
}


def find_tool(name: str, extra_candidates=()) -> str:
    exe = shutil.which(name)
    if exe:
        return exe
    for cand in extra_candidates:
        if Path(cand).is_file():
            return str(cand)
    sys.exit(f"[fw] ERROR: {name} not found")


def run(cmd, env=None):
    print("[fw] " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, env=env)


def main():
    home = Path(os.environ.get("USERPROFILE", str(Path.home())))
    sdk = Path(os.environ.get("PICO_SDK_PATH", home / ".pico-sdk" / "sdk" / "2.3.1"))
    toolchain = Path(os.environ.get("PICO_TOOLCHAIN_PATH", home / ".pico-sdk" / "toolchain" / "15_2_Rel1"))

    if not (sdk / "external" / "pico_sdk_import.cmake").is_file():
        sys.exit(f"[fw] ERROR: pico-sdk not found at {sdk}")
    if not (toolchain / "bin").is_dir():
        sys.exit(f"[fw] ERROR: ARM toolchain not found at {toolchain}")

    cmake = find_tool("cmake", (
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    ))
    ninja = find_tool("ninja", (home / ".platformio" / "packages" / "tool-ninja" / "ninja.exe",))

    env = os.environ.copy()
    env["PICO_SDK_PATH"] = str(sdk)
    env["PICO_TOOLCHAIN_PATH"] = str(toolchain)

    BUILD_DIR.mkdir(exist_ok=True)
    run([
        cmake, "-S", HERE, "-B", BUILD_DIR, "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        "-DPICO_BOARD=pico",
        "-DPICO_PLATFORM=rp2040",
        "-DPICO_NO_PICOTOOL=1",
        "-DCMAKE_BUILD_TYPE=Release",
    ], env=env)
    run([cmake, "--build", BUILD_DIR], env=env)

    OUT_DIR.mkdir(exist_ok=True)
    for ms, out_name in TARGETS.items():
        src = BUILD_DIR / f"blink_{ms}.bin"
        if not src.is_file():
            sys.exit(f"[fw] ERROR: {src} not found")
        blob = src.read_bytes()
        if len(blob) < 512:
            sys.exit(f"[fw] ERROR: {src} too small ({len(blob)} bytes)")
        if blob[0] != 0x00 or blob[1] != 0xB5:
            print(f"[fw] WARNING: {src} does not start with boot2 push {{lr}}")
        dst = OUT_DIR / out_name
        dst.write_bytes(blob)
        print(f"[fw] {out_name}: {len(blob)} bytes")

    print("[fw] OK")


if __name__ == "__main__":
    main()
