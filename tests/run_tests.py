#!/usr/bin/env python3
"""Run the DMG accuracy test suite against the headless emulator.

Downloads c-sp/game-boy-test-roms (Blargg, Mooneye, dmg-acid2, ...) into
tests/roms on first use, runs every DMG-relevant test in parallel and prints
a pass/fail table. Exits non-zero if any *required* test fails.

Usage: tests/run_tests.py [--emu build/gbemu] [--filter SUBSTRING] [-j N] [-v]
"""

import argparse
import concurrent.futures
import io
import os
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROMS = ROOT / "tests" / "roms"
RELEASE = "https://github.com/c-sp/game-boy-test-roms/releases/download/v7.0/game-boy-test-roms-v7.0.zip"

# Tests that need hardware details this emulator deliberately doesn't model
# (scanline renderer instead of a pixel FIFO, no OAM corruption bug, ...).
BEST_EFFORT = {
    "blargg/dmg_sound/rom_singles/09-wave read while on.gb",
    "blargg/dmg_sound/rom_singles/10-wave trigger while on.gb",
    "blargg/dmg_sound/rom_singles/12-wave write while on.gb",
    "mooneye-test-suite/acceptance/ppu/intr_2_mode0_timing_sprites.gb",
    "mooneye-test-suite/acceptance/ppu/lcdon_timing-GS.gb",
    "mooneye-test-suite/acceptance/ppu/lcdon_write_timing-GS.gb",
    "mooneye-test-suite/acceptance/ppu/stat_lyc_onoff.gb",
}
BEST_EFFORT_PREFIXES = ("blargg/oam_bug/",)


def fetch_roms():
    if (ROMS / "blargg").is_dir():
        return
    print(f"Downloading test ROMs from {RELEASE} ...", flush=True)
    ROMS.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(RELEASE) as resp:
        data = resp.read()
    zipfile.ZipFile(io.BytesIO(data)).extractall(ROMS)


def mooneye_runs_on_dmg(stem: str) -> bool:
    """Mooneye encodes target models in the file name suffix, e.g. -GS or -dmgABCmgb."""
    if "-" not in stem:
        return True
    suffix = stem.rsplit("-", 1)[1]
    if suffix.startswith("dmgABC"):
        return True
    # Group letters: G = DMG/MGB, S = SGB, C = CGB, A = AGB. Lowercase names are specific models.
    return suffix.isupper() and "G" in suffix


def collect():
    tests = []  # (name, args)
    b = ROMS / "blargg"
    singles = sorted((b / "cpu_instrs" / "individual").glob("*.gb"))
    for rom in singles + [b / "cpu_instrs" / "cpu_instrs.gb"]:
        tests.append((rom, []))
    tests.append((b / "instr_timing" / "instr_timing.gb", []))
    for rom in sorted((b / "mem_timing" / "individual").glob("*.gb")) + [b / "mem_timing" / "mem_timing.gb"]:
        tests.append((rom, []))
    for rom in sorted((b / "mem_timing-2" / "rom_singles").glob("*.gb")) + [b / "mem_timing-2" / "mem_timing.gb"]:
        tests.append((rom, []))
    tests.append((b / "halt_bug.gb", []))
    for rom in sorted((b / "dmg_sound" / "rom_singles").glob("*.gb")):
        tests.append((rom, []))
    for rom in sorted((b / "oam_bug" / "rom_singles").glob("*.gb")):
        tests.append((rom, []))

    m = ROMS / "mooneye-test-suite"
    for rom in sorted((m / "acceptance").rglob("*.gb")):
        if mooneye_runs_on_dmg(rom.stem):
            tests.append((rom, []))
    for mbc in ("mbc1", "mbc2", "mbc5"):
        for rom in sorted((m / "emulator-only" / mbc).glob("*.gb")):
            tests.append((rom, []))

    acid = ROMS / "dmg-acid2"
    tests.append((acid / "dmg-acid2.gb", ["--compare", str(acid / "dmg-acid2-dmg.png")]))
    return [(str(rom.relative_to(ROMS)), rom, args) for rom, args in tests]


def run_one(emu, rom, args, timeout_sec):
    cmd = [emu, "--headless", "--quiet", "--timeout-sec", str(timeout_sec), *args, str(rom)]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return "TIMEOUT", "host timeout"
    status = {0: "PASS", 1: "FAIL", 2: "TIMEOUT"}.get(proc.returncode, "ERROR")
    return status, (proc.stdout + proc.stderr).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emu", default=str(ROOT / "build" / "gbemu"))
    ap.add_argument("--filter", default="")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("-v", action="store_true", help="show emulator output for failures")
    opts = ap.parse_args()

    if not Path(opts.emu).exists():
        sys.exit(f"emulator not found at {opts.emu}; build it first (cmake --build build)")
    fetch_roms()
    tests = [t for t in collect() if opts.filter in t[0]]

    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=opts.j) as pool:
        futures = {pool.submit(run_one, opts.emu, rom, args, 120): name for name, rom, args in tests}
        for fut in concurrent.futures.as_completed(futures):
            results[futures[fut]] = fut.result()

    required_fail = 0
    counts = {"required": [0, 0], "best effort": [0, 0]}
    for name, _, _ in tests:
        status, output = results[name]
        best = name in BEST_EFFORT or name.startswith(BEST_EFFORT_PREFIXES)
        kind = "best effort" if best else "required"
        counts[kind][1] += 1
        if status == "PASS":
            counts[kind][0] += 1
        elif not best:
            required_fail += 1
        tag = "" if not best else "  (best effort)"
        print(f"{status:8} {name}{tag}")
        if opts.v and status != "PASS":
            print("         " + output.replace("\n", "\n         "))

    print()
    for kind, (ok, total) in counts.items():
        print(f"{kind:12}: {ok}/{total} passed")
    sys.exit(1 if required_fail else 0)


if __name__ == "__main__":
    main()
