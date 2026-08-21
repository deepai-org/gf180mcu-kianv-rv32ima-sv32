# Linux on KianV RTL

This target boots a pinned KianV OpenSBI/Linux/BusyBox image on the complete
SoC RTL under Verilator. It drives the real SPI-flash, SDRAM, and UART pins; it
does not replace the CPU with an instruction-set emulator.

The test stops only after it reaches an interactive shell through the modeled
UART RX pin, runs `uname`, starts two concurrent shell workers, observes both
workers finish, and prints `SCHEDULER_OK`. KianV has one hart, so this proves
Linux process creation, timer interrupts, context switching, and scheduling;
it does not claim multicore execution.

## What differs from the hardware demo

The author's hardware image mounts its root filesystem from an SD card. This
simulation embeds a deliberately small BusyBox initramfs in the kernel so the
first reproducible target depends only on the already-tested SDRAM and UART
models. The CPU, SV32 MMU/TLBs, caches, CLINT timer, OpenSBI, Linux kernel, and
userspace processes are still exercised. Adding the hardware-demo SD rootfs is
an optional second stage, not a prerequisite for demonstrating Linux boot.

The testbench initializes the modeled external SDRAM with `fw_payload.bin`.
Reset fetch still traverses the RTL SPI-flash controller and pins, where a tiny
two-instruction boot image jumps to the payload at `0x80000000`; the simulation
does not spend cycles copying the multi-megabyte payload from flash. This is an
external-memory loading shortcut, not a CPU, cache, or MMU shortcut.

## Pinned inputs

- KianV: `da994e6c25b0667d6579922f4bab8d800d19e944`
- Buildroot: `256aa8ed85f8fd65ea0f0f242adb55f95a13eb2b`
- Linux: `7.1-rc1`, plus KianV's patch set
- OpenSBI: `1.4`, KianV platform and patch set
- Ubuntu build container: immutable multi-architecture image digest in the
  `Dockerfile`

The Buildroot configuration is intentionally stored in this repository. The
artifact script copies only the KianV board, package, and OpenSBI additions
from the pinned upstream checkout, then substitutes the simulation defconfig,
initramfs overlay, and device-tree boot arguments.

## Run it

Requirements are Docker, GNU Make 4.3 or newer, and Verilator. On an ARM64
host:

```sh
make -C sim/linux artifacts  # build OpenSBI + Linux + BusyBox in Docker
make -C sim/linux build      # compile the full SoC RTL simulator
make -C sim/linux verify     # rebuild incrementally, boot, and check milestones
```

`make verify` writes the complete guest UART transcript to
`sim/linux/build/uart.log`. The generated binaries and cloned dependencies stay
under the ignored `sim/linux/build/` directory. `SHA256SUMS` records the three
principal artifacts. Make tracks the artifact inputs, so repeated `build`,
`run`, and `verify` invocations do not enter Docker unless a build input changed.

A port-compatible generated `soc` can reuse the same harness with
`RTL=/absolute/path/to/soc.v`, a separate `OBJDIR`, and
`EXTRA_CFLAGS=-DLOOM_IMPORTED_RTL`. The generated design must use the same
`SIM` elaboration as this target; the macro only removes progress diagnostics
that depend on the source RTL's internal Verilator names.

Useful overrides:

```sh
make -C sim/linux run MAX_CYCLES=5000000000
CONTAINER_CPUS=8 CONTAINER_MEMORY=16g make -C sim/linux artifacts
```

The default simulation ceiling is three billion RTL clock cycles. This is a
functional ceiling, not a modeled wall-clock boot deadline: Verilator runs far
slower than the 20 MHz clock represented by the RTL. The last full verification
passed after 2,265,395,523 modeled cycles and took about 40 minutes on one
Neoverse-V2 host core; Verilator simulation is effectively single-core here.

The test feeds commands through the real 16-byte RTL UART receive FIFO. It
paces bytes by 2.5 ms and waits for a response before sending the next command,
which models interactive typing and avoids treating an instantaneous host-side
paste as if the ASIC had an unlimited FIFO.

## Success criteria

The final UART log must contain all of:

```text
KianV RTL Linux ready
COMMAND_OK
Linux kianv
WORKER_A_DONE
WORKER_B_DONE
SCHEDULER_OK
```

The simulator itself requires the shell prompt, drains UART input, sees live
`/proc/<pid>/stat` entries for both workers in one process-table snapshot, and
checks that worker B's five-second sleep completes before worker A's ten-second
sleep before returning success. The procfs snapshot uses shell builtins because
starting a complete `ps | grep` pipeline itself takes several modeled seconds
on this multicycle RTL core. Disabling terminal echo in `/init` prevents
injected command text from satisfying these checks by itself.

## Troubleshooting

- If the artifact build was interrupted, rerun it; Buildroot resumes from the
  existing ignored output tree.
- If Docker reports an architecture mismatch, set `CONTAINER_PLATFORM` to the
  host's Linux platform. This repository's primary tested path is ARM64.
- If simulation reaches `MAX_CYCLES`, inspect the end of `build/uart.log` and
  rerun with a larger value. Progress lines on stderr include the RTL PC and
  memory-bus address every ten million cycles.
- `make clean` deletes every Linux artifact and dependency checkout, so the
  next artifact build starts from network downloads.
