#!/usr/bin/env bash
set -euo pipefail

TAPEOUT_TAG=GF180MCU_Tapeout_Dec2025
TAPEOUT_COMMIT=1892792dd2e77df37443cabf0e69320b08492827
PDK_TAG=1.6.6
PDK_COMMIT=fb4b8f59451d248ef5b310a593759f946a8f6ae8
REFERENCE_ZIP_SHA=d149a25bff5523c51018f0ba2bba006e7491e25d19fc7ac8b5175825cedc8a58
REFERENCE_GDS_SHA=09dd61160c252740fa5cb7efabcc98ddd5f0a179b436b7f2302e445f2f1cca3d
COMPARE_WORK_DIR=

die() {
  echo "error: $*" >&2
  exit 1
}

preflight() {
  test "$(git rev-parse "$TAPEOUT_TAG^{commit}")" = "$TAPEOUT_COMMIT" ||
    die "unexpected $TAPEOUT_TAG commit"

  local protected=(Makefile flake.lock flake.nix cocotb fpga ip librelane scripts src)
  git diff --quiet "$TAPEOUT_TAG" -- "${protected[@]}" ||
    die "tapeout inputs differ from $TAPEOUT_TAG"

  local available_kib required_kib
  available_kib=$(df -Pk . | awk 'NR == 2 {print $4}')
  required_kib=$(( ${MIN_FREE_GIB:-80} * 1024 * 1024 ))
  (( available_kib >= required_kib )) ||
    die "less than ${MIN_FREE_GIB:-80} GiB free disk space"

  local available_mem_kib required_mem_kib
  available_mem_kib=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
  required_mem_kib=$(( ${MIN_MEMORY_GIB:-16} * 1024 * 1024 ))
  (( available_mem_kib >= required_mem_kib )) ||
    die "less than ${MIN_MEMORY_GIB:-16} GiB available memory"

  printf '%s  %s\n' "$REFERENCE_ZIP_SHA" gds/chip_top.gds.zip | sha256sum -c -
  local actual_gds_sha
  actual_gds_sha=$(python -c \
    'import hashlib, sys, zipfile; print(hashlib.sha256(zipfile.ZipFile(sys.argv[1]).read("chip_top.gds")).hexdigest())' \
    gds/chip_top.gds.zip)
  test "$actual_gds_sha" = "$REFERENCE_GDS_SHA" ||
    die "reference GDS hash mismatch"

  echo "preflight passed"
  echo "host architecture: $(uname -m)"
  nix --version
  librelane --version
}

fetch_pdk() {
  if [ ! -d gf180mcu/.git ]; then
    git clone --depth 1 --branch "$PDK_TAG" https://github.com/wafer-space/gf180mcu.git gf180mcu
  fi
  test "$(git -C gf180mcu rev-parse HEAD)" = "$PDK_COMMIT" ||
    die "gf180mcu is not pinned to $PDK_COMMIT"
  echo "PDK pinned at $PDK_COMMIT"
}

simulate() {
  fetch_pdk
  make sim
}

build_gds() {
  fetch_pdk
  make SLOT=1x1 librelane
  make copy-final
}

verify() {
  local comparison_mode=${1:-strict}
  test -f final/gds/chip_top.gds || die "final/gds/chip_top.gds is missing"
  sha256sum final/gds/chip_top.gds
  if printf '%s  %s\n' "$REFERENCE_GDS_SHA" final/gds/chip_top.gds | sha256sum -c -; then
    echo "generated GDS is byte-identical to the submitted reference"
  else
    echo "generated GDS differs byte-for-byte; running layout XOR" >&2
    compare_reference "$comparison_mode"
  fi
}

run_all() {
  preflight
  build_gds
  verify allow-mismatch
  echo "all stages completed successfully"
}

compare_reference() {
  local comparison_mode=${1:-strict}
  test -f final/gds/chip_top.gds || die "final/gds/chip_top.gds is missing"

  local work_dir xor_script xor_count
  work_dir=$(mktemp -d "${TMPDIR:-/tmp}/kianv-reference-xor.XXXXXX")
  COMPARE_WORK_DIR=$work_dir
  cleanup_compare() {
    case "$COMPARE_WORK_DIR" in
      "${TMPDIR:-/tmp}"/kianv-reference-xor.*) rm -rf -- "$COMPARE_WORK_DIR" ;;
      *) die "refusing to clean unexpected comparison path: $COMPARE_WORK_DIR" ;;
    esac
  }
  trap cleanup_compare EXIT

  python - "$work_dir/reference.gds" <<'PY'
import shutil
import sys
import zipfile

with zipfile.ZipFile("gds/chip_top.gds.zip") as archive:
    with archive.open("chip_top.gds") as source, open(sys.argv[1], "wb") as target:
        shutil.copyfileobj(source, target)
PY

  printf '%s  %s\n' "$REFERENCE_GDS_SHA" "$work_dir/reference.gds" | sha256sum -c -
  xor_script=$(python -c \
    'from pathlib import Path; import librelane; print(Path(librelane.__file__).parent / "scripts/klayout/xor.drc")')

  ruby "$xor_script" \
    --output "$work_dir/reference-xor.xml" \
    --top chip_top \
    --threads "${XOR_THREADS:-28}" \
    --ignore "" \
    "$work_dir/reference.gds" final/gds/chip_top.gds \
    2>&1 | tee "$work_dir/reference-xor.log"

  xor_count=$(awk '/Total XOR differences:/ {count=$NF} END {print count}' \
    "$work_dir/reference-xor.log")
  test -n "$xor_count" || die "KLayout XOR did not report a difference count"
  if (( xor_count == 0 )); then
    echo "generated GDS is geometrically identical to the submitted reference"
  else
    echo "generated GDS has $xor_count geometric XOR differences from the submitted reference" >&2
    if [[ "$comparison_mode" == allow-mismatch ]]; then
      echo "note: the completed flow is valid, but its GDS is not geometrically identical to the reference"
      echo "note: direct 'verify' and 'compare' stages return status 2 for this result"
      return 0
    fi
    return 2
  fi
}

stage=${1:-all}
case "$stage" in
  preflight) preflight ;;
  pdk) preflight; fetch_pdk ;;
  sim) preflight; simulate ;;
  gds) preflight; build_gds ;;
  verify) preflight; verify ;;
  compare) preflight; compare_reference ;;
  all) run_all ;;
  *) die "unknown stage '$stage' (expected preflight, pdk, sim, gds, verify, compare, or all)" ;;
esac
