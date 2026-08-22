#!/usr/bin/env python3
"""Assemble and hash the reviewable outputs of a completed Loom physical run."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REQUIRED_FINAL = (
    "gds/chip_top.gds",
    "def/chip_top.def",
    "nl/chip_top.nl.v",
    "odb/chip_top.odb",
    "sdc/chip_top.sdc",
    "metrics.json",
)
REPORT_STAGES = (
    "54-openroad-stapostpnr",
    "55-openroad-irdropreport",
    "60-klayout-antenna",
    "64-klayout-density",
    "66-magic-drc",
    "67-klayout-drc",
    "72-netgen-lvs",
    "78-misc-reportmanufacturability",
)
REQUIRED_ZERO_METRICS = (
    "antenna__violating__nets",
    "antenna__violating__pins",
    "design__critical_disconnected_pin__count",
    "design__instance_unmapped__count",
    "design__lvs_device_difference__count",
    "design__lvs_error__count",
    "design__lvs_net_difference__count",
    "design__lvs_property_fail__count",
    "design__lvs_unmatched_device__count",
    "design__lvs_unmatched_net__count",
    "design__lvs_unmatched_pin__count",
    "design__power_grid_violation__count",
    "design__xor_difference__count",
    "klayout__antenna_error__count",
    "klayout__density_error__count",
    "klayout__drc_error__count",
    "magic__drc_error__count",
    "magic__illegal_overlap__count",
    "route__antenna_violation__count",
    "route__drc_errors",
    "timing__hold__tns",
    "timing__hold__wns",
)


def command_output(*command: str) -> str:
    result = subprocess.run(
        command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=True,
    )
    return result.stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def copy_stage(run_dir: Path, stage_prefix: str, signoff_dir: Path) -> None:
    matches = sorted(run_dir.glob(f"{stage_prefix}*"))
    if len(matches) != 1:
        raise SystemExit(
            f"expected one {stage_prefix!r} stage in {run_dir}, found {len(matches)}"
        )
    source = matches[0]
    target = signoff_dir / source.name
    target.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.rpt", "*.log", "reports/*", "summary.rpt", "irdrop.rpt"):
        for item in source.glob(pattern):
            if item.is_file():
                relative = item.relative_to(source)
                destination = target / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, destination)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--final", type=Path, default=ROOT / "final")
    args = parser.parse_args()
    run_dir = args.run_dir.resolve()
    final_dir = args.final.resolve()

    if not run_dir.is_dir():
        raise SystemExit(f"run directory does not exist: {run_dir}")
    for relative in REQUIRED_FINAL:
        path = final_dir / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise SystemExit(f"required final artifact is missing or empty: {path}")

    metrics = json.loads((final_dir / "metrics.json").read_text())
    for name in REQUIRED_ZERO_METRICS:
        if name not in metrics:
            raise SystemExit(f"release metric is absent: {name}")
        if metrics[name] != 0:
            raise SystemExit(f"release metric is nonzero: {name}={metrics[name]}")
    final_netlist = (final_dir / "nl" / "chip_top.nl.v").read_text()
    # Yosys/OpenROAD may derive tie-cell names from a replaced instance (for
    # example ``u_d6_antenna_14052``).  Count only retained antenna-cell
    # instances whose escaped hierarchical name ends exactly in
    # ``.u_d6_antenna``; a substring count incorrectly includes those tie
    # cells and rejected the otherwise clean signoff run.
    targeted_diodes = len(re.findall(
        r"(?m)^\s*gf180mcu_fd_sc_mcu9t5v0__antenna\s+"
        r"\\\S*\.u_d6_antenna\s+\(\.I\(",
        final_netlist,
    ))
    if targeted_diodes != 21:
        raise SystemExit(
            "final netlist does not retain exactly 21 targeted SRAM antenna "
            f"diodes: found {targeted_diodes}"
        )

    spef_dir = final_dir / "spef"
    spef_dir.mkdir(parents=True, exist_ok=True)
    rcx_matches = sorted(run_dir.glob("53-openroad-rcx*"))
    if len(rcx_matches) != 1:
        raise SystemExit(f"expected exactly one RCX stage, found {len(rcx_matches)}")
    for source in sorted(rcx_matches[0].glob("*/chip_top.*.spef")):
        shutil.copy2(source, spef_dir / source.name)
    if len(list(spef_dir.glob("chip_top.*.spef"))) != 3:
        raise SystemExit("expected min, nominal, and max extracted SPEF files")

    shutil.copy2(run_dir / "resolved.json", final_dir / "run-resolved.json")
    shutil.copy2(run_dir / "flow.log", final_dir / "flow.log")
    signoff_dir = final_dir / "signoff"
    if signoff_dir.exists():
        shutil.rmtree(signoff_dir)
    for stage in REPORT_STAGES:
        copy_stage(run_dir, stage, signoff_dir)

    provenance = {
        "schema": 1,
        "run": run_dir.name,
        "source_commit": command_output("git", "rev-parse", "HEAD"),
        "source_status": command_output("git", "status", "--short"),
        "pdk_commit": command_output("git", "-C", "gf180mcu", "rev-parse", "HEAD"),
        "nix": command_output("nix", "--version"),
        "yosys": command_output("yosys", "-V"),
        "librelane": command_output("librelane", "--version"),
        "handoff_manifest_sha256": sha256(ROOT / "reproduce/loom-physical-handoff.json"),
    }
    provenance_path = final_dir / "RELEASE_PROVENANCE.json"
    provenance_path.write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n")

    sums_path = final_dir / "SHA256SUMS"
    files = sorted(
        path for path in final_dir.rglob("*")
        if path.is_file() and path != sums_path
    )
    sums_path.write_text(
        "".join(f"{sha256(path)}  {path.relative_to(final_dir)}\n" for path in files)
    )
    subprocess.run(["sha256sum", "-c", sums_path.name], cwd=final_dir, check=True)
    print(f"release bundle archived from {run_dir.name}: {len(files)} hashed files")


if __name__ == "__main__":
    main()
