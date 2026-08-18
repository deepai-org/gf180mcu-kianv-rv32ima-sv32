#!/usr/bin/env bash
set -euo pipefail

TAPEOUT_TAG=GF180MCU_Tapeout_Dec2025
TAPEOUT_COMMIT=1892792dd2e77df37443cabf0e69320b08492827
PDK_TAG=1.6.6
PDK_COMMIT=fb4b8f59451d248ef5b310a593759f946a8f6ae8
REFERENCE_ZIP_SHA=d149a25bff5523c51018f0ba2bba006e7491e25d19fc7ac8b5175825cedc8a58
REFERENCE_GDS_SHA=09dd61160c252740fa5cb7efabcc98ddd5f0a179b436b7f2302e445f2f1cca3d

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
  test -f final/gds/chip_top.gds || die "final/gds/chip_top.gds is missing"
  sha256sum final/gds/chip_top.gds
  if printf '%s  %s\n' "$REFERENCE_GDS_SHA" final/gds/chip_top.gds | sha256sum -c -; then
    echo "generated GDS is byte-identical to the submitted reference"
  else
    echo "generated GDS differs byte-for-byte; inspect sign-off reports and run layout XOR" >&2
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
  all) preflight; simulate; build_gds; verify ;;
  *) die "unknown stage '$stage' (expected preflight, pdk, sim, gds, verify, or all)" ;;
esac
