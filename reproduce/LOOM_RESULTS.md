# Loom physical-flow results

This file records complete physical runs of the Loom-generated KianV handoff.
It is evidence, not a fabrication waiver: only a run that clears the release
gate in [`../LOOM_PHYSICAL_FLOW.md`](../LOOM_PHYSICAL_FLOW.md) is a candidate
for submission.

## Run 1: diagnostic route

- Run directory: `RUN_2026-08-21_04-55-11`
- Wall time: 6 hours 40 minutes
- Neutral/emitted physical RTL SHA-256:
  `62ff5cc1ea28e759f5039c8327df7b1c57b77f0011b7fe823d958bb4cbbd12f1`
- Final filled GDS SHA-256:
  `d7de8ec7793dc5611ccd2437c81144896b066854a8e6849ac111720216bd8a81`
- Final state SHA-256:
  `78b6dc0dc083f81bb1423bac34ed474404b7dd9a1687c28ea93335f95220eb00`
- Release status: **failed closed; do not fabricate this run**

The run completed all 78 configured stages. It established that the checked
Loom hierarchy, 21 fixed SRAMs, directional PDN, placement, clock tree,
routing, extraction, and LVS all pass through the pinned toolchain. It did not
clear final foundry-deck DRC and antenna checks:

- OpenROAD detailed-route DRC: 0.
- OpenROAD antenna: 0 violating nets/pins after 122 inserted diodes.
- KLayout density: 0.
- Netgen LVS: circuits match; all LVS error/difference counts are 0.
- Hold timing: WNS/TNS 0 at every corner.
- KLayout antenna: 2 `ANT.16_ii_ANT.3` Metal2 markers. Both ratios were
  413.639 against a limit of 400 and had zero diode area.
- KLayout DRC: 2 edge-pair markers for one `M3.2b` SRAM-boundary spacing
  site. Magic represented the same site as 5 fragments.

The handoff generator was subsequently changed to test heuristic diode
insertion at threshold 130 and a one-GCell macro routing extension. Run 2
showed that the generic threshold was much too broad; it is not part of the
release handoff.

## Comparison with the pinned upstream reproduction

These figures compare Run 1 with the ARM64 upstream-RTL reproduction in
[`RESULTS.md`](RESULTS.md). They do not compare against unavailable signoff
reports for the originally fabricated GDS.

| Metric | Upstream reproduction | Loom Run 1 | Difference |
| --- | ---: | ---: | ---: |
| Standard cells | 135,941 | 143,539 | +5.59% |
| Standard-cell area | 3,855,640 | 4,024,560 | +4.38% |
| Routed wire length | 9,626,558 | 11,374,736 | +18.16% |
| Worst setup WNS (`max_ss_125C_3v00`) | -21.0179 ns | -24.7227 ns | -3.7048 ns |
| TT/max-RC setup margin | 2.9592 ns | 2.4848 ns | -0.4744 ns |
| Worst max-slew count | 5,998 | 7,334 | +22.27% |
| Worst max-capacitance count | 2,737 | 3,202 | +16.99% |
| Worst nominal IR drop, VDD | 30.95 uV | 22.85 uV | -26.17% |
| Worst nominal IR drop, VSS | 26.90 uV | 23.85 uV | -11.32% |

Like the upstream reproduction, Run 1 meets the 33 ns constraint at all TT
and fast corners and violates setup at the six characterized 3.00 V slow
corners. Its electrical-constraint counts and slow-corner timing are worse,
while nominal IR drop is lower. This is not a timing-clean signoff claim.

## Run 2: broad-diode diagnostic

- Run directory: `RUN_2026-08-21_17-00-24`
- Wall time before deliberate termination: approximately 3 hours
- Release status: **superseded diagnostic; do not fabricate this run**

Run 2 tested the one-GCell macro extension together with LibreLane's generic
130-unit heuristic diode threshold. Post-CTS repair completed normally. Before
the heuristic stage, it used 142,300 standard cells and 12,544,938 um of
estimated global-route wire, modest improvements over Run 1. The heuristic
stage then inserted 99,124 antenna cells, raising the standard-cell count to
241,424. Although OpenROAD subsequently reduced its LEF-visible antenna count
to zero, burdening nearly every long-net sink is not a suitable reproduction.
The run was stopped before detailed route and foundry-deck checks.

The two Run 1 foundry markers were then localized precisely. They lie at
x=2745.035--2745.635 um in the north-oriented instruction-cache SRAM tile 3,
or x=366.035--366.635 um relative to the macro origin. The SRAM LEF places
`D[6]` at x=365.150--366.270 um and declares its 1.152 um^2 gate area, exactly
matching the deck markers. The release handoff now contains one kept GF180
antenna diode on `D[6]` in each of the 21 SRAM wrappers and forbids the broad
heuristic stage. Its post-Yosys preflight checks the exact cell and connection.

## Run 3: targeted repair

The targeted physical RTL SHA-256 is
`a7cc437452ec765ffbe79372f0cb5d83b311102a5d4f02ba91d5129c87cb3d14`.
The complete targeted route is the remaining release gate. Its result must be
recorded here after all foundry-deck, LVS, timing, and artifact checks finish.
