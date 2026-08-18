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

On a Linux host with Docker:

```sh
./reproduce/run-in-docker.sh preflight
./reproduce/run-in-docker.sh all
```

The full LibreLane run can take several hours. Each stage can also be run
separately; see [reproduce/README.md](reproduce/README.md).
