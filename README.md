# KianV SV32 (MMU) RV32IMA Zicntr Zicsr Zifencei SSTC Linux/XV6 SoC

A 32-bit RISC-V Linux SoC taped out on GF180MCU.

Really glad to get the MPW opportunity from [wafer.space](https://wafer.space)!

## Run xv6 in RTL simulation

The complete tapeout SoC RTL now boots the matching KianV xv6 port to a shell
under Verilator. The reproducible software build, pin-level SDRAM/SPI models,
verification command, expected runtime, and fidelity notes are documented in
[`sim/xv6/README.md`](sim/xv6/README.md). The one-command entry point is:

```sh
make -C sim/xv6
```

## Reproduce the submitted GDS

The `reproduce-gf180-run1` branch is based on the immutable
`GF180MCU_Tapeout_Dec2025` tag. Reproduction helpers do not alter the taped-out
RTL or physical-design configuration.

Clean-machine ARM64 quick start:

```sh
git clone --branch reproduce-gf180-run1 \
  https://github.com/deepai-org/gf180mcu-kianv-rv32ima-sv32.git
cd gf180mcu-kianv-rv32ima-sv32
./reproduce/run-in-docker.sh preflight
./reproduce/run-in-docker.sh all
```

The recorded full run took approximately 5 hours 22 minutes after tool
download. It completed successfully but produced a physically different,
sign-off-clean routed layout rather than an identical copy of the submitted
GDS. Each stage, expected result, exit status, and troubleshooting procedure is
documented in [reproduce/README.md](reproduce/README.md).
