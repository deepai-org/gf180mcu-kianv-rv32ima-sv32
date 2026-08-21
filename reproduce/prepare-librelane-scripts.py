#!/usr/bin/env python3
"""Create a hash-checked LibreLane script tree that honors resource sharing."""

from __future__ import annotations

import hashlib
import pathlib
import shutil
import sys

from librelane.common import get_script_dir


SOURCE_SHA256 = "c657253cf91bd15e59e1484ffcabf68f07918d4572fb4323c905135e1ffff013"
PATCHED_SHA256 = "0347846bf33bb88513581bdd37b650dcc51f4c915fb1dd564a70597aeb0e723b"
REPLACEMENTS = (
    (
        "    report_dir,\n    *,\n",
        "    report_dir,\n    share_resources,\n    *,\n",
    ),
    (
        '    d.run_pass("share")  # Share logic across the design\n',
        "    if share_resources:\n"
        '        d.run_pass("share")  # Share logic across the design\n',
    ),
    (
        "        report_dir,\n        booth=config[\"SYNTH_MUL_BOOTH\"],\n",
        "        report_dir,\n"
        "        config[\"SYNTH_SHARE_RESOURCES\"],\n"
        "        booth=config[\"SYNTH_MUL_BOOTH\"],\n",
    ),
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} OUTPUT_SCRIPTS_DIR")
    source = pathlib.Path(get_script_dir())
    destination = pathlib.Path(sys.argv[1])
    if destination.exists():
        raise SystemExit(f"error: destination already exists: {destination}")
    synth = source / "pyosys" / "synthesize.py"
    source_bytes = synth.read_bytes()
    if digest(source_bytes) != SOURCE_SHA256:
        raise SystemExit("error: pinned LibreLane synthesis script hash changed")
    source_text = source_bytes.decode()
    for old, _new in REPLACEMENTS:
        if source_text.count(old) != 1:
            raise SystemExit("error: expected LibreLane synthesis patch site is absent")

    shutil.copytree(source, destination)
    for old, new in REPLACEMENTS:
        source_text = source_text.replace(old, new, 1)
    patched = source_text.encode()
    if digest(patched) != PATCHED_SHA256:
        raise SystemExit("error: patched LibreLane synthesis script hash changed")
    (destination / "pyosys" / "synthesize.py").write_bytes(patched)
    print(
        "LIBRELANE_LOOM_SCRIPT_PATCH_PASS "
        f"source_sha256={SOURCE_SHA256} patched_sha256={PATCHED_SHA256}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
