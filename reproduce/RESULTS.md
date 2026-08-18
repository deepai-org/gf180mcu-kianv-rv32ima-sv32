# ARM64 reproduction result

Run date: 2026-08-18  
Run directory: `RUN_2026-08-18_04-35-16`  
Host/container architecture: native `aarch64` / `linux/arm64`

The pinned LibreLane flow completed with exit code 0 and copied all final
views to `final/`. The generated layout is a clean implementation of the
tagged tapeout inputs, but it is not the same geometry as the submitted GDS.

## Artifact comparison

| Artifact | SHA-256 |
| --- | --- |
| Submitted `chip_top.gds` | `09dd61160c252740fa5cb7efabcc98ddd5f0a179b436b7f2302e445f2f1cca3d` |
| ARM64 reproduced `final/gds/chip_top.gds` | `71f97369a2d51506ba7f42b1910ef8f548246d8efd94fba199056d2f91c27a7c` |

The pinned KLayout layer-by-layer comparison found **13,936,919 XOR
polygons**. Fixed macro and marker layers matched, while routed, contact, and
fill layers differed. A native ARM64 flow rerun therefore does not recreate
the submitted geometry byte-for-byte or geometrically. Run `compare` to
repeat this check.

## Physical verification

- Detailed-route DRC: 0 violations.
- OpenROAD final antenna check: 0 violating nets and pins.
- Magic/KLayout stream-out XOR before seal/fill: 0 differences.
- KLayout antenna: 0 violations.
- KLayout density: 0 violations.
- KLayout GF180 DRC: 0 violations across all reported rules.
- Magic DRC: 2 duplicate markers for one `CO.3` location; KLayout reports
  `CO.3: 0` on the final GDS.
- Netgen LVS: circuits match uniquely; 89,909 devices and 88,919 nets on
  each side, with all LVS error counts at 0.
- Nominal IR analysis: VDD and VSS are connected; worst reported drops are
  30.9 uV and 26.9 uV. LibreLane warns that no `VSRC_LOC_FILES` was supplied,
  so these values are not a package-aware IR sign-off.

## Timing result

All hold corners and all TT/fast setup corners have zero negative slack. The
3.00 V slow setup corners retain violations:

| Corner | Setup WNS | Setup TNS |
| --- | ---: | ---: |
| `max_ss_125C_3v00` | -21.017936 | -125811.657119 |
| `max_ss_n40C_3v00` | -5.881280 | -14893.140781 |
| `min_ss_125C_3v00` | -15.710388 | -93340.017795 |
| `min_ss_n40C_3v00` | -2.007791 | -789.848479 |
| `nom_ss_125C_3v00` | -18.128610 | -107952.956893 |
| `nom_ss_n40C_3v00` | -3.787193 | -5922.329077 |

LibreLane also reports max-slew/max-capacitance violations and 5,117
unannotated nets. These warnings are retained rather than presenting the run
as timing-clean at every characterized corner.
