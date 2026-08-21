# xv6 on the tapeout RTL

This simulation runs the KianV xv6 port on the `soc` RTL used by this
repository's GF180MCU tapeout. It verifies the CPU, RV32IMA/Sv32 MMU, caches,
UART, SPI controllers, and external SDRAM transactions together. Success means
the UART reaches an interactive xv6 shell prompt.

## Quick start

On Debian/Ubuntu, install:

```sh
sudo apt-get install build-essential git make verilator gcc-riscv64-linux-gnu \
  binutils-riscv64-linux-gnu
```

Then run from the repository root:

```sh
make -C sim/xv6
```

The target performs all of the following:

1. Fetches the pinned `splinedrive/kianRiscV` commit
   `da994e6c25b0667d6579922f4bab8d800d19e944`.
2. Applies the small, reviewable [`xv6-sim.patch`](xv6-sim.patch).
3. Builds the matching RV32 KianV xv6 kernel and filesystem.
4. Builds the complete SoC RTL with Verilator.
5. Runs until UART contains all three boot milestones:
   `xv6 kernel is booting`, `init: starting sh`, and `$ `.

Generated files are contained under `sim/xv6/build/` and ignored by Git.
The captured console is `sim/xv6/build/uart.log`. Use `make -C sim/xv6 clean`
to remove the simulator, fetched source, software artifacts, and logs.

For a port-compatible generated RTL file, override `RTL` and use a separate
ignored object directory. `LOOM_IMPORTED_RTL` disables only the progress
message's dependency on Verilator's source-specific internal names:

```sh
make -C sim/xv6 verify RTL=/absolute/path/to/soc.v \
  OBJDIR=build/generated-obj EXTRA_CFLAGS=-DLOOM_IMPORTED_RTL
```

The generated RTL must match this harness's `SIM` elaboration. In particular,
the simulation UART divider resets to 174; the taped-out configuration resets
it to 1 and expects software to configure it.

## Expected runtime and result

The reference run on a 32-vCPU ARM64 host reached the shell after
`222,410,634` 20 MHz RTL clock cycles (11.12 seconds of modeled time), taking
approximately four minutes of host time with Verilator 5.020. Typical final
UART output is:

```text
xv6 kernel is booting

init: starting sh
$
```

The simulator emits a progress line with the RTL program counter and memory
address every ten million clocks. Override the default 300-million-clock
timeout when diagnosing a slower build:

```sh
make -C sim/xv6 MAX_CYCLES=500000000
```

## Simulation architecture

[`sim_main.cpp`](sim_main.cpp) instantiates the repository's `soc` module and
connects functional pin-level models for:

- a 32 MiB, 16-bit SDR SDRAM connected to the controller's actual command,
  bank, address, byte-mask, and bidirectional data pins;
- SPI NOR mode `0x03` reads;
- an SPI-mode SDHC card backed by the generated xv6 filesystem image; and
- the 115200-baud UART output.

The SRAM macro wrappers use their existing synthesizable behavioral branch;
the GF180 PDK is therefore not required for RTL simulation.

The harness places an eight-byte ROM at the real reset-vector flash offset.
It contains `lui t0, 0x80000; jalr zero, 0(t0)`. The xv6 kernel is preloaded in
the SDRAM model at `0x80000000`, while `fs.img` is placed at the SD-card offset
expected by xv6. Thus the CPU still fetches its first instructions through the
RTL SPI-NOR controller and all kernel execution and filesystem I/O go through
the tapeout RTL, but this fast path deliberately skips the board bootloader's
32 MiB clear and RLE decompression loops.

## Intentional simulation-only changes

The pinned upstream xv6 tree is never vendored or modified in place.
`xv6-sim.patch` is applied inside the ignored build directory and makes two
runtime reductions:

- xv6 uses 2 MiB of the modeled 32 MiB SDRAM instead of byte-filling 8 MiB at
  startup;
- SD initialization delays are capped at one software tick because the
  behavioral card responds immediately.

Neither change touches the tapeout RTL. The peripheral models are functional
models, not transistor, I/O-voltage, or SDRAM timing-signoff models. SD writes
are retained for the duration of a run but are not written back to `fs.img`.

## Useful component targets

```sh
make -C sim/xv6 artifacts  # only fetch and build xv6.bin and fs.img
make -C sim/xv6 build      # only build the Verilator executable
make -C sim/xv6 run        # build artifacts/RTL and run without grepping
make -C sim/xv6 verify     # run and enforce all UART milestones
```
