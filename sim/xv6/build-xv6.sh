#!/usr/bin/env bash
set -euo pipefail

upstream_url=https://github.com/splinedrive/kianRiscV.git
upstream_commit=da994e6c25b0667d6579922f4bab8d800d19e944
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir="$script_dir/build"
source_dir="$build_dir/kianRiscV"
xv6_dir="$source_dir/linux_socs/kianv_mc_rv32ima_sv32/os/xv6"

mkdir -p "$build_dir"
if [[ ! -d "$source_dir/.git" ]]; then
  git init -q "$source_dir"
  git -C "$source_dir" remote add origin "$upstream_url"
fi
git -C "$source_dir" fetch --depth 1 origin "$upstream_commit"
git -C "$source_dir" checkout -q --detach --force FETCH_HEAD
git -C "$source_dir" apply --unidiff-zero "$script_dir/xv6-sim.patch"

if command -v riscv32-unknown-elf-gcc >/dev/null 2>&1; then
  cross=riscv32-unknown-elf-
  linker=riscv32-unknown-elf-ld
elif command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
  cross=riscv64-unknown-elf-
  linker="riscv64-unknown-elf-ld -m elf32lriscv"
elif command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
  cross=riscv64-linux-gnu-
  linker="riscv64-linux-gnu-ld -m elf32lriscv"
else
  echo "No RISC-V GCC toolchain found." >&2
  echo "Install riscv64-linux-gnu-gcc or use upstream's xv6 Dockerfile." >&2
  exit 1
fi

make_args=(
  -f Makefile ARCH=riscv
  "CROSS_COMPILE=$cross" "TOOLPREFIX=$cross"
  "CC=${cross}gcc" "AS=${cross}gcc" "LD=$linker"
  "AR=${cross}ar" "OBJCOPY=${cross}objcopy"
  "OBJDUMP=${cross}objdump" "STRIP=${cross}strip"
)

make -C "$xv6_dir" "${make_args[@]}" clean
make -C "$xv6_dir" "${make_args[@]}" -j"$(nproc)"
make -C "$xv6_dir" "${make_args[@]}" -j"$(nproc)" fs.img
"${cross}objcopy" -O binary "$xv6_dir/kernel/kernel" "$build_dir/xv6.bin"
cp "$xv6_dir/fs.img" "$build_dir/fs.img"
sha256sum "$build_dir/xv6.bin" "$build_dir/fs.img" > "$build_dir/SHA256SUMS"

echo "Built pinned KianV xv6 artifacts:"
ls -lh "$build_dir/xv6.bin" "$build_dir/fs.img"
