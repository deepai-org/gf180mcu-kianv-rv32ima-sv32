#!/usr/bin/env python3
"""Verify the committed Loom RTL is bound to the intended LibreLane hierarchy."""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "reproduce" / "loom-physical-handoff.json"
SRAM = "gf180mcu_fd_ip_sram__sram512x8m8wm1"
SRAM_WRAPPER = "gf180mcu_fd_ip_sram__sram512x8m8wm1_wrapper"
ANTENNA = "gf180mcu_fd_sc_mcu9t5v0__antenna"
ANTENNA_INSTANCE = "u_d6_antenna"
LOOM_GENERATOR_COMMIT = "6222955d60b0290f44440a94d83c3102ce7350e2"
LOOM_GENERATOR_SHA256 = "2cf49a8d9063549aa75df37f83f0907ddbe28e2e4485fa9d009a7307510e0981"


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text())
    generator = manifest.get("generator", {})
    if (
        generator.get("loom_commit") != LOOM_GENERATOR_COMMIT
        or generator.get("sha256") != LOOM_GENERATOR_SHA256
        or not generator.get("path", "").endswith("/scripts/kianv_physical_handoff.py")
    ):
        fail("unexpected Loom physical-handoff generator provenance")
    rtl = ROOT / manifest["outputs"]["physical_rtl"]["path"]
    config = ROOT / manifest["outputs"]["librelane_config"]["path"]
    pdn = ROOT / manifest["outputs"]["pdn_config"]["path"]
    wrapper = ROOT / manifest["inputs"]["foundry_wrapper"]["path"]
    helpers = ROOT / manifest["inputs"]["physical_helpers"]["path"]
    source_config = ROOT / manifest["inputs"]["librelane_config"]["path"]
    for path, entry in (
        (rtl, manifest["outputs"]["physical_rtl"]),
        (config, manifest["outputs"]["librelane_config"]),
        (pdn, manifest["outputs"]["pdn_config"]),
        (wrapper, manifest["inputs"]["foundry_wrapper"]),
        (helpers, manifest["inputs"]["physical_helpers"]),
        (source_config, manifest["inputs"]["librelane_config"]),
    ):
        if not path.is_file() or digest(path) != entry["sha256"]:
            fail(f"hash mismatch: {path.relative_to(ROOT)}")
    if manifest.get("sram_instances") != 21:
        fail("the known-good floorplan requires exactly 21 SRAM instances")
    if manifest.get("synthesis_options") != {"SYNTH_SHARE_RESOURCES": False}:
        fail("unexpected Loom synthesis options")
    if manifest.get("physical_options") != {"GRT_MACRO_EXTENSION": 1}:
        fail("unexpected Loom physical repair options")
    if manifest.get("sram_input_antenna_diodes") != {
        "cell": ANTENNA,
        "instance": ANTENNA_INSTANCE,
        "pin": "D[6]",
        "instances": 21,
    }:
        fail("unexpected SRAM input antenna repair")
    config_text = config.read_text()
    if config_text.count("SYNTH_SHARE_RESOURCES: false") != 1:
        fail("Loom config must disable pathological SAT resource sharing")
    for setting in ("GRT_MACRO_EXTENSION: 1",):
        if config_text.count(setting) != 1:
            fail(f"Loom config must contain {setting}")
    for forbidden in ("RUN_HEURISTIC_DIODE_INSERTION:", "HEURISTIC_ANTENNA_THRESHOLD:"):
        if forbidden in config_text:
            fail(f"broad heuristic antenna repair must remain disabled: {forbidden}")
    if config_text.count("PDN_CFG: dir::pdn_cfg.loom.tcl") != 1:
        fail("translated Loom PDN config is not selected")
    expected_paths = sorted(manifest["sram_path_translation"].values())
    pdn_text = pdn.read_text()
    if any(pdn_text.count(path) != 1 for path in expected_paths):
        fail("translated PDN config does not name every SRAM exactly once")

    with tempfile.TemporaryDirectory(prefix="kianv-loom-hierarchy-") as directory:
        hierarchy_json = pathlib.Path(directory) / "hierarchy.json"
        command = (
            f"read_verilog -sv -I {ROOT / 'src'} -DSLOT_1X1 "
            "-DGF180 -DUSE_POWER_PINS "
            f"{rtl} {ROOT / 'src/loom_physical_helpers.v'} "
            f"{ROOT / 'src/chip_top.sv'}; "
            "hierarchy -top chip_top; proc; "
            f"write_json {hierarchy_json}"
        )
        result = subprocess.run(
            ["yosys", "-Q", "-p", command], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if result.returncode:
            fail("Yosys hierarchy failed:\n" + result.stdout[-6000:])
        modules = json.loads(hierarchy_json.read_text())["modules"]

    wrapper_module = modules.get(SRAM_WRAPPER)
    if wrapper_module is None:
        fail("physical SRAM wrapper is absent after Yosys hierarchy")
    diode = wrapper_module.get("cells", {}).get(ANTENNA_INSTANCE)
    if diode is None or diode.get("type") != ANTENNA:
        fail("SRAM wrapper does not contain the exact antenna cell")
    d_bits = wrapper_module.get("ports", {}).get("D", {}).get("bits", [])
    if len(d_bits) != 8 or diode.get("connections", {}).get("I") != [d_bits[6]]:
        fail("SRAM antenna cell is not connected exactly to D[6]")

    found: list[str] = []

    def visit(module: str, path: list[str], ancestors: set[str]) -> None:
        if module in ancestors:
            fail(f"recursive hierarchy at {module}")
        for name, cell in modules[module].get("cells", {}).items():
            next_path = path + [name]
            child = cell["type"]
            if child == SRAM:
                found.append(".".join(next_path))
            elif child in modules and not modules[child].get("attributes", {}).get("blackbox"):
                visit(child, next_path, ancestors | {module})

    visit("chip_top", [], set())
    expected = expected_paths
    if sorted(found) != expected:
        fail("post-Yosys SRAM hierarchy does not match fixed macro placements")
    print(
        "KIANV_LOOM_HANDOFF_PASS "
        f"sram_instances={len(found)} antenna_diodes={len(found)} "
        f"rtl_sha256={digest(rtl)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
