# Loom physical-flow handoff

This branch contains a fabrication-oriented Loom conversion of the KianV
`chip_core`. It reuses the pinned GF180 PDK, die, pad ring, timing constraints,
power grid, hard-SRAM coordinates, and all other settings from the known-good
KianV tapeout flow.

The physical input is [`src/chip_core.loom.v`](src/chip_core.loom.v), selected
by [`librelane/config.loom.yaml`](librelane/config.loom.yaml). Do not give the
neutral conversion artifact directly to LibreLane: its executable SRAM model
is intentionally replaced here by the real
`gf180mcu_fd_ip_sram__sram512x8m8wm1` primitive.
The fixed macro coordinates and the directional SRAM PDN groups are both
translated to the emitted hierarchy; the latter is in
[`librelane/pdn_cfg.loom.tcl`](librelane/pdn_cfg.loom.tcl).

## Artifact map

The checked-in handoff is intentionally split by responsibility:

| File | Purpose | Edit policy |
| --- | --- | --- |
| `src/chip_core.loom.v` | Synthesizable Loom-generated `chip_core` hierarchy | Regenerate; do not hand-edit |
| `src/loom_physical_helpers.v` | Small foundry-SRAM binding used only by the physical flow | Review and edit as source |
| `librelane/config.loom.yaml` | Loom-specific LibreLane inputs and translated macro placements | Regenerate; do not hand-edit |
| `librelane/pdn_cfg.loom.tcl` | Translated directional SRAM PDN groups | Regenerate; do not hand-edit |
| `reproduce/loom-physical-handoff.json` | Source/output hashes and complete hierarchy-path mapping | Regenerate; do not hand-edit |
| `reproduce/verify-loom-handoff.py` | Fail-closed structural and provenance preflight | Review and edit as source |

The emitted RTL preserves the important KianV module and instance names, such
as `chip_core`, `soc`, `icache`, and `dcache`. Parameter-specialized helper
modules and internal SSA nets use injective generated names. Treat the file as
a compiler artifact: navigate and diagnose it by hierarchy, but make semantic
changes in the source conversion and regenerate it.

## Fast preflight

From an environment containing Yosys:

```sh
make verify-loom-handoff
```

This fails unless:

- the complete source tree is clean and committed, so the recorded source
  commit identifies every physical-flow input outside the handoff manifest;
- the generated RTL, physical configuration, original foundry wrapper, and
  source configuration match their recorded hashes;
- `chip_top` can specialize the 54-pad `chip_core` interface;
- Yosys finds exactly 21 GF180 SRAM primitives; and
- every SRAM wrapper contains one kept GF180 antenna diode connected exactly
  to macro input `D[6]`; and
- every post-Yosys SRAM path exactly matches its fixed placement entry.

The Loom config sets `SYNTH_SHARE_RESOURCES: false`. This disables an optional
Yosys SAT optimization that becomes pathological on Loom's explicit TLB
memory-read representation (multi-million-variable sharing queries). The
pinned LibreLane revision declares that option but accidentally invokes the
pass unconditionally. `loom-preflight` therefore hash-checks the affected
LibreLane script, copies its script tree to a temporary directory, patches the
one invocation to honor the existing option, and runs LibreLane through
`reproduce/run-librelane-overlay.py` to select that copy. It does not mutate
the pinned Nix store. This does not disable memory lowering or technology
mapping, and it does not alter RTL behavior; the 74-module equivalence proof is
against the pre-physical neutral artifact.

The first complete Loom route reached final DRC and LVS. LVS, detailed-route
DRC, hold timing, and density were clean, but the foundry decks found one
M3.2b spacing site at a fixed SRAM boundary and two marginal Metal2 antenna
markers inside the north-oriented instruction-cache tile 3 SRAM. Their
x-offsets align exactly with the macro LEF's `D[6]` pin/input buffer. The
physical wrapper now retains one GF180 antenna diode on `D[6]` for each of the
21 SRAMs, and the config extends macro routing blockages by one global-routing
cell. A diagnostic 130-unit heuristic threshold inserted 99,124 diodes and is
explicitly forbidden by the verifier. The bounded implementation-only repair
is hash-bound in the manifest. A clean complete rerun remains the fabrication
release gate.

Complete-run measurements and failed-closed diagnostics are recorded in
[`reproduce/LOOM_RESULTS.md`](reproduce/LOOM_RESULTS.md).

The complete path translation and artifact provenance are recorded in
[`reproduce/loom-physical-handoff.json`](reproduce/loom-physical-handoff.json).
That manifest also records the exact Loom generator SHA-256 and Loom source
commit, currently `6222955d60b0290f44440a94d83c3102ce7350e2`.

## Pinned physical run

On the supported ARM64 Docker host:

```sh
./reproduce/run-in-docker.sh loom-preflight
./reproduce/run-in-docker.sh loom-all
```

`loom-all` downloads/verifies the pinned PDK, runs the complete LibreLane flow,
copies every final view to `final/`, requires nonempty GDS and metrics outputs,
adds the min/nominal/max SPEFs and signoff reports, records tool/PDK/source
provenance, fails closed on nonzero DRC/antenna/LVS/density/hold and related
release metrics, verifies `final/SHA256SUMS`, and prints the bundle hashes. The
equivalent command inside the pinned Nix
environment is:

```sh
make SLOT=1x1 librelane-loom
make copy-final
```

The submitted GDS must come from `final/gds/chip_top.gds` produced by this Loom
flow. The original committed GDS is a geometric reference for the upstream
RTL, not an acceptable substitute for a Loom run.

## Regeneration

The sibling Loom checkout regenerates the checked physical artifacts from the
hash-bound neutral package:

```sh
cd /path/to/lean_hw
scripts/verify_kianv_conversion.sh \
  /path/to/gf180mcu-kianv-rv32ima-sv32 build/kianv-physical-proof
python3 scripts/kianv_physical_handoff.py \
  --emitted build/kianv-physical-proof/conversion/chip_core.loom.v \
  --package build/kianv-physical-proof/conversion/chip_core.package.import.json \
  --kianv-root /path/to/gf180mcu-kianv-rv32ima-sv32 \
  --output-rtl /path/to/gf180mcu-kianv-rv32ima-sv32/src/chip_core.loom.v \
  --output-config /path/to/gf180mcu-kianv-rv32ima-sv32/librelane/config.loom.yaml \
  --output-pdn /path/to/gf180mcu-kianv-rv32ima-sv32/librelane/pdn_cfg.loom.tcl \
  --manifest /path/to/gf180mcu-kianv-rv32ima-sv32/reproduce/loom-physical-handoff.json
```

Commit all four outputs (RTL, config, PDN config, and manifest) together.
`verify-loom-handoff` detects partial or manually edited regeneration.

## Fabrication release gate

Do not release this design from the hierarchy preflight alone. Archive the
complete `librelane/runs/RUN_*` directory and verify, from that same run:

- LibreLane exits successfully and all configured signoff steps execute;
- setup/hold timing, max slew, and max capacitance are reviewed at every
  configured corner;
- Magic and KLayout DRC, antenna, and Netgen LVS results are acceptable;
- PDN generation, IR-drop reporting assumptions, density, and manufacturability
  reports are reviewed;
- the final GDS, netlist, SDC, SPEF, DEF, ODB, metrics, and tool/PDK revisions
  are hash-archived together; and
- the final GDS is inspected in KLayout with all 21 SRAM macros and the intended
  pad ring present.

Logical equivalence and RTL boot evidence cover the Loom conversion. They do
not replace post-route timing, extraction, DRC, LVS, antenna, or foundry
submission checks.
