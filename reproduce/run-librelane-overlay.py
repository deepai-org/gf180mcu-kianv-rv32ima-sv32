#!/usr/bin/env python3
"""Run pinned LibreLane with an explicitly selected checked script tree."""

from __future__ import annotations

import os
import runpy

import librelane.common


script_dir = os.environ.get("KIANV_LIBRELANE_SCRIPT_DIR")
if not script_dir:
    raise SystemExit("error: KIANV_LIBRELANE_SCRIPT_DIR is unset")


def get_checked_script_dir() -> str:
    return script_dir


librelane.common.get_script_dir = get_checked_script_dir
runpy.run_module("librelane", run_name="__main__")
