#!/usr/bin/env bash
set -euo pipefail

kianv_url=https://github.com/splinedrive/kianRiscV.git
kianv_commit=da994e6c25b0667d6579922f4bab8d800d19e944
buildroot_url=https://github.com/buildroot/buildroot.git
buildroot_commit=256aa8ed85f8fd65ea0f0f242adb55f95a13eb2b

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir="$script_dir/build"
kianv_dir="$build_dir/kianRiscV"
buildroot_dir="$build_dir/buildroot"
upstream_profile="$kianv_dir/linux_socs/kianv_mc_rv32ima_sv32/os/linux/buildroot-kianv-soc/bldroot"

mkdir -p "$build_dir"

fetch_commit() {
  local url=$1 commit=$2 directory=$3
  if [[ ! -d "$directory/.git" ]]; then
    git init -q "$directory"
    git -C "$directory" remote add origin "$url"
  fi
  git -C "$directory" fetch --depth 1 origin "$commit"
  git -C "$directory" checkout -q --detach --force FETCH_HEAD
}

fetch_commit "$kianv_url" "$kianv_commit" "$kianv_dir"
fetch_commit "$buildroot_url" "$buildroot_commit" "$buildroot_dir"

rm -rf "$buildroot_dir/board/kianv"
cp -a "$upstream_profile/board/kianv" "$buildroot_dir/board/"
cp -a "$upstream_profile/boot/." "$buildroot_dir/boot/"
cp -a "$upstream_profile/package/." "$buildroot_dir/package/"

# Upstream's OpenSBI objects.mk patch leaves a trailing space on the XLEN
# assignment. GNU Make consequently misses its `ifeq (..., 32)` branches and
# places the RV32 Linux payload at 0x80200000 instead of the required 4 MiB
# boundary, 0x80400000. Correct the copied patch before Buildroot applies it.
sed -i 's/^+PLATFORM_RISCV_XLEN = 32 $/+PLATFORM_RISCV_XLEN = 32/' \
  "$buildroot_dir/boot/opensbi/0001-added-kianv-platform.patch"

install -D -m 0644 "$script_dir/configs/kianv_rtl_linux_defconfig" \
  "$buildroot_dir/configs/kianv_rtl_linux_defconfig"
install -D -m 0644 "$script_dir/configs/linux-sim.fragment" \
  "$buildroot_dir/board/kianv/rtl-sim/linux-sim.fragment"
install -d "$buildroot_dir/board/kianv/rtl-sim/rootfs-overlay"
cp -a "$script_dir/rootfs-overlay/." \
  "$buildroot_dir/board/kianv/rtl-sim/rootfs-overlay/"

# The embedded initramfs is the root filesystem; no MMC is needed for this
# first deterministic RTL target. The physical memory description remains the
# ASIC's complete 32 MiB.
sed -i \
  's#bootargs = ".*";#bootargs = "earlycon=sbi console=hvc0 rdinit=/init loglevel=7";#' \
  "$buildroot_dir/board/kianv/rv32ima_sv32/kianv.dts"
dtc -I dts -O dtb -o \
  "$buildroot_dir/board/kianv/rv32ima_sv32/kianv.dtb" \
  "$buildroot_dir/board/kianv/rv32ima_sv32/kianv.dts" -S 4096

make -C "$buildroot_dir" kianv_rtl_linux_defconfig
make -C "$buildroot_dir" opensbi-dirclean
make -C "$buildroot_dir" linux-configure

# Buildroot adds CONFIG_INITRAMFS_SOURCE after merging kernel fragments, so the
# compression choice is not visible while those fragments are processed. Set
# it after that fixup. An uncompressed archive avoids spending billions of RTL
# cycles inflating data that is already resident in simulated SDRAM.
linux_dir="$buildroot_dir/output/build/linux-7.1-rc1"
"$linux_dir/scripts/config" --file "$linux_dir/.config" \
  --disable INITRAMFS_COMPRESSION_GZIP \
  --enable INITRAMFS_COMPRESSION_NONE
make -C "$linux_dir" ARCH=riscv \
  CROSS_COMPILE="$buildroot_dir/output/host/bin/riscv32-buildroot-linux-gnu-" \
  olddefconfig

make -C "$buildroot_dir" -j"$(nproc)"

# Buildroot assembles the initramfs into Linux during filesystem finalization,
# after its normal OpenSBI package step. Rebuild OpenSBI once more so the
# payload embeds that final Image rather than the pre-initramfs kernel.
make -C "$buildroot_dir" opensbi-rebuild

cp "$buildroot_dir/output/images/fw_payload.bin" "$build_dir/fw_payload.bin"
cp "$buildroot_dir/output/images/Image" "$build_dir/Image"
cp "$buildroot_dir/output/images/rootfs.cpio" "$build_dir/rootfs.cpio"

payload_size=$(stat -c %s "$build_dir/fw_payload.bin")
if (( payload_size > 32 * 1024 * 1024 )); then
  echo "error: fw_payload.bin does not fit the KianV 32 MiB SDRAM" >&2
  exit 1
fi

(
  cd "$build_dir"
  sha256sum fw_payload.bin Image rootfs.cpio > SHA256SUMS
)

echo "Built pinned KianV Linux artifacts:"
ls -lh "$build_dir/fw_payload.bin" "$build_dir/Image" "$build_dir/rootfs.cpio"
