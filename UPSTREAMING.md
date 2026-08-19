# Proposed upstream contributions

Last reviewed: 2026-08-19

This fork contains reproducibility and RTL-simulation work that has not been
submitted upstream. The upstream GF180 repository's
[pull-request page](https://github.com/splinedrive/gf180mcu-kianv-rv32ima-sv32/pulls)
currently reports that new pull-request creation is restricted, so this file
records what we would offer and what must be done before submission. No pull
request URL exists yet.

## Repository state

- This work is on `reproduce-gf180-run1` in
  `deepai-org/gf180mcu-kianv-rv32ima-sv32`.
- The branch starts at the immutable `GF180MCU_Tapeout_Dec2025` tag,
  `1892792dd2e77df37443cabf0e69320b08492827`.
- At the last review, `splinedrive/gf180mcu-kianv-rv32ima-sv32` `main` was
  `2483ecb9cc00f52dbc2a226fda348d8c139e707c`, two commits past the tapeout
  tag. A submission must account for those commits rather than presenting this
  branch as current upstream `main`.
- At the last review, `splinedrive/kianRiscV` `master` was
  `da994e6c25b0667d6579922f4bab8d800d19e944`.

## 1. Fix the RV32 OpenSBI payload address

Target: [`splinedrive/kianRiscV`](https://github.com/splinedrive/kianRiscV)

Proposed title:

```text
fix: remove trailing whitespace from OpenSBI RV32 XLEN setting
```

The KianV OpenSBI patch currently adds this assignment:

```make
PLATFORM_RISCV_XLEN = 32<space>
```

Here, `<space>` denotes the otherwise invisible trailing character. It becomes
part of the Make value, so subsequent
`ifeq ($(PLATFORM_RISCV_XLEN), 32)` conditions do not match. The RV32 Linux
payload is consequently linked with the 64-bit fallback offset at
`0x80200000`, rather than the required 4 MiB-aligned address `0x80400000`.

Our Linux builder repairs the copied patch in `sim/linux/build-linux.sh` before
Buildroot applies it. The upstream contribution should instead remove the one
trailing space in
`linux_socs/kianv_mc_rv32ima_sv32/os/linux/buildroot-kianv-soc/bldroot/boot/opensbi/0001-added-kianv-platform.patch`.

Status: confirmed against current `kianRiscV` `master`; not yet prepared as a
standalone commit in that repository. This is the smallest and highest-priority
candidate.

## 2. Add the full-RTL xv6 Verilator smoke test

Target:
[`splinedrive/gf180mcu-kianv-rv32ima-sv32`](https://github.com/splinedrive/gf180mcu-kianv-rv32ima-sv32)

Local implementation: commit `6def9d3` and `sim/xv6/`

Proposed title:

```text
sim: add reproducible xv6 full-SoC Verilator smoke test
```

The test instantiates the complete `soc` RTL with functional SDRAM, SPI NOR,
SPI SD-card, and UART pin models. It boots the pinned KianV xv6 port through
the real RV32IMA/Sv32 core, caches, MMU, memory controller, and peripherals.
The verified run reached the xv6 shell after 222,410,634 modeled 20 MHz cycles,
in approximately four host minutes.

Before submission:

1. Rebase the change onto upstream `main` at or after `2483ecb`.
2. Rerun `make -C sim/xv6 verify` against that tree and record the result.
3. Decide with the maintainer whether the roughly four-minute target belongs
   in pull-request CI or remains an explicit/manual regression.
4. Keep the simulation-only xv6 memory-size and SD-delay patch local to the
   harness; it is not proposed as a general xv6 behavior change.

## 3. Add the tagged-tapeout GDS reproduction workflow

Target:
[`splinedrive/gf180mcu-kianv-rv32ima-sv32`](https://github.com/splinedrive/gf180mcu-kianv-rv32ima-sv32)

Local implementation: commits `3a07ca0`, `38d9c6b`, `b2012b8`, and `9410e07`;
`reproduce/`

Proposed title:

```text
repro: document and automate the GF180 Dec 2025 tapeout reproduction
```

The workflow pins the tapeout tag, PDK, Nix/LibreLane environment, and
reference hashes; performs resource and input guards; runs the physical flow;
copies final views; and compares the generated GDS against the submitted GDS.
`reproduce/RESULTS.md` records the native ARM64 sign-off and timing evidence,
including the geometric mismatch and electrical/timing violations rather than
claiming an identical result.

The recorded run completed the flow in approximately 5 hours 22 minutes. It
was sign-off-clean for the documented DRC/LVS checks, but its GDS was neither
byte-identical nor geometrically identical to the submitted artifact.

Before submission:

1. Choose an upstream integration model with the maintainer. The current
   preflight deliberately requires the protected physical-design inputs to
   match the tapeout tag, while upstream `main` now contains post-tapeout RTL.
   A version merged to `main` should run the flow in an isolated worktree at
   the tag, or upstream should preserve it on a dedicated reproduction branch.
2. Replace the fork-specific clone instructions with the selected upstream
   branch/worktree procedure.
3. Preserve `RESULTS.md` as measured evidence, clearly distinct from expected
   results on other host architectures.
4. Keep this as a manual archival workflow; its disk, memory, and five-hour
   runtime make it inappropriate for routine pull-request CI.

## 4. Add the Linux/BusyBox MMU and scheduler regression

Target:
[`splinedrive/gf180mcu-kianv-rv32ima-sv32`](https://github.com/splinedrive/gf180mcu-kianv-rv32ima-sv32)

Local implementation: commit `97d135b` and `sim/linux/`

Proposed title:

```text
sim: boot Linux and exercise the scheduler on full KianV RTL
```

The self-checking Verilator target boots pinned OpenSBI, Linux 7.1-rc1, and a
BusyBox initramfs on the complete SoC RTL. It reaches an interactive shell,
runs `uname`, observes two distinct worker PIDs live in one procfs snapshot,
and verifies that their five- and ten-second sleeps complete in scheduler
order. The verified run passed after 2,265,395,523 modeled cycles in about 40
host minutes.

Before submission:

1. Land or incorporate the `kianRiscV` OpenSBI whitespace fix, then remove the
   corresponding workaround from `build-linux.sh`.
2. Rebase onto current GF180 upstream `main` and rerun the complete verifier.
3. Factor the duplicated SDRAM, SPI-flash, and UART models shared with the xv6
   harness into `sim/common/` so the two tests cannot drift independently.
4. Test the artifact build on x86-64 as well as the already-verified ARM64
   path, because upstream GitHub Actions currently uses x86-64 runners.
5. Keep the 40-minute Linux boot manual, scheduled, or opt-in rather than a
   required check on every small pull request.
6. Retain the fidelity note that firmware is preloaded into modeled external
   SDRAM and reset executes a small SPI-fetched jump; the CPU, MMU/TLBs,
   caches, timer, SDRAM controller, and userspace execution are not bypassed.

## Suggested submission order

1. The one-line `kianRiscV` OpenSBI correction.
2. The xv6 full-RTL smoke test.
3. The tagged GDS reproduction workflow, after agreeing on tag/worktree
   integration.
4. The Linux regression after extracting the common simulation models.

Each item should remain an independently reviewable PR. Generated build trees,
firmware images, logs, and regenerated GDS files should not be committed.

## Submission checklist

- Confirm with the maintainer how contributions should be submitted while the
  GitHub repository restricts new pull requests.
- Refresh both upstream heads recorded above.
- Rebase or recreate each proposed branch from its intended upstream base.
- Run the exact documented verifier after the rebase.
- Include measured output and fidelity limits in the PR description.
- Link back to this fork for the longer reproduction record when useful.
- Record the resulting PR URL and disposition in this file.
