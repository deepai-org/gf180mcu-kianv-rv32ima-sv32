# KianV GF180 chip area and routing analysis

This analysis uses the reproduced final OpenROAD database, DEF, and metrics. It
separates additive silicon area from attached routing: metal overlaps the cells
and SRAM beneath it, so adding metal rectangle area to cell area would
double-count the physical footprint.

## Whole-die composition

The final layout is 3.932 mm x 5.122 mm, or 20.140 mm2. The internal core
region is 12.902 mm2.

| Physical use | Area | Die share |
|---|---:|---:|
| Functional standard cells | 3.081 mm2 | 15.3% |
| Cache SRAM macros | 4.397 mm2 | 21.8% |
| Pads and pad spacers | 5.271 mm2 | 26.2% |
| Filler cells | 4.708 mm2 | 23.4% |
| Tap and endcap cells | 0.770 mm2 | 3.8% |
| Die ID/artwork | 0.082 mm2 | 0.4% |
| Unoccupied geometric area | 1.826 mm2 | 9.1% |

OpenROAD's reported 3.856 mm2 standard-cell area includes tap, endcap, and
related physical-only cells. The hierarchy-attributed functional logic itself
is 3.081 mm2.

The SRAMs form a ring around the central standard-cell logic:

```text
                 I-cache SRAMs
       +------+------+------+------+------+
       |      |      |      |      |      |
D-cache SRAMs |       central CPU / SoC logic       | I-cache SRAMs
down left side|                                      | down right side
       |      |                                      |
       +------+-+---- I-cache SRAMs ----+---+--------+
```

Consequently, the cells and routing belonging to several logical components
are interleaved rather than occupying clean rectangular blocks.

## Functional components

The area share below is relative to 7.479 mm2 of functional standard-cell plus
SRAM area. Signal wire length and vias are reported separately because routing
is above, rather than beside, the device area.

| Component | Logic | SRAM | Total area | Functional share | Routed wire | Vias |
|---|---:|---:|---:|---:|---:|---:|
| I-cache | 0.342 | 2.932 | **3.274 mm2** | **43.8%** | 0.589 m | 43,141 |
| CPU core | 2.065 | - | **2.065 mm2** | **27.6%** | **6.901 m** | 416,060 |
| D-cache | 0.127 | 1.466 | **1.592 mm2** | **21.3%** | 0.187 m | 16,418 |
| SDRAM interface/controller | 0.101 | - | 0.101 mm2 | 1.4% | 0.501 m | 24,138 |
| UART | 0.085 | - | 0.085 mm2 | 1.1% | 0.110 m | 13,247 |
| SoC bus and glue | 0.064 | - | 0.064 mm2 | 0.9% | 0.535 m | 12,269 |
| CLINT/timer | 0.054 | - | 0.054 mm2 | 0.7% | 0.078 m | 10,320 |
| PLIC | 0.025 | - | 0.025 mm2 | 0.3% | 0.027 m | 3,597 |
| SPI0 and SPI1 | 0.023 | - | 0.023 mm2 | 0.3% | 0.033 m | 3,760 |
| SPI NOR interface | 0.021 | - | 0.021 mm2 | 0.3% | 0.035 m | 3,141 |
| Cache interconnect | 0.009 | - | 0.009 mm2 | 0.1% | 0.034 m | 1,229 |
| Unattributed/ambiguous | 0.141 | - | 0.141 mm2 | 1.9% | 0.505 m | 37,023 |

The small GPIO, system-information block, clock divider, and chip-level glue
make up most of the unlisted remainder.

### Cache interpretation

The 21 SRAM macros are all GF180 512x8 instances, approximately 0.2094 mm2
each:

- The I-cache uses fourteen macros, organized as two ways of seven byte-wide
  macros. It holds 2 x 512 x 4 bytes = 4 KiB of instruction data, with tags in
  the same 56-bit-wide macro construction.
- The D-cache uses seven macros. It holds 512 x 4 bytes = 2 KiB of data, again
  with tags in the macro construction.
- Valid and replacement state are standard-cell registers, accounting for part
  of the cache-control logic area.
- There is no general-purpose on-chip SRAM beyond these caches.

The caches contain about 65% of functional device area but only about 8% of
signal wire length. The SRAM macros are much denser than synthesized
flip-flop-based storage.

## CPU breakdown

| CPU component | Cell area | CPU share | Routed wire |
|---|---:|---:|---:|
| DTLB | **0.414 mm2** | **20.1%** | 0.591 m |
| ITLB | **0.409 mm2** | **19.8%** | 0.582 m |
| MMU, page walker, and translation logic | **0.262 mm2** | **12.7%** | **2.524 m** |
| Integer register file | 0.228 mm2 | 11.1% | 0.200 m |
| CSR unit, counters, and exception state | 0.213 mm2 | 10.3% | 0.414 m |
| Datapath, ALU, and muxes | 0.119 mm2 | 5.8% | 0.561 m |
| Control FSM and decode | 0.096 mm2 | 4.7% | 0.615 m |
| Multiplier | 0.092 mm2 | 4.5% | 0.174 m |
| CPU top-level glue | 0.095 mm2 | 4.6% | 0.819 m |
| Divider | 0.068 mm2 | 3.3% | 0.144 m |
| Unattributed CPU boundary logic | 0.067 mm2 | 3.3% | 0.273 m |

The main CPU observations are:

- The MMU, ITLB, and DTLB together occupy about 1.085 mm2, or 52.5% of the
  CPU's standard-cell area.
- They account for approximately 3.70 m, or 53.6%, of CPU-attributed wiring.
- Each physical-build TLB is configured as 32 entries and four-way associative.
  VPN, ASID, PTE, validity, and replacement state are synthesized from standard
  cells rather than SRAM macros, making them comparatively expensive in
  GF180MCU.
- The 32x32-bit integer register file is also synthesized from standard cells.
- The CSR block includes 64-bit cycle, time, and retired-instruction counters,
  as well as the machine/supervisor state required by Linux and NetBSD.
- This is a multicycle CPU, not a conventional five-stage pipeline. The control
  FSM is therefore an explicit physical component.
- The hardware multiplier and divider are modest compared with the MMU/TLB
  structures.

## Routing result

The final DEF contains exactly:

- **9.626558 m of regular signal routing**
- **588,855 routed-via records**

This wire length exactly matches OpenROAD's final route metric. The CPU owns
approximately 71.7% of attributed routing despite being only 27.6% of
functional device area. The long MMU/page-walker, SoC-bus, and SDRAM routes
reflect both their fanout and the SRAM-ring floorplan.

This accounting excludes the shared power-grid `SPECIALNETS`; VDD and VSS
distribution cannot usefully be assigned to one functional block.

## Method and limitations

Macro areas, aggregate areas, total signal-wire length, and via counts are
exact measurements from the final physical database and DEF. Per-block
attribution was reconstructed from preserved hierarchical net names, then
propagated through inserted buffers and anonymous synthesized nets.

The attribution covers approximately 95.4% of functional cell area and 94.8%
of routed wire. Per-component figures should therefore be treated as a
hierarchy-aware physical-netlist estimate, not as exact partition boundaries.
In particular, synthesized boundary logic may reasonably be assigned to either
side of a subsystem interface.

Primary artifacts:

- [`final/metrics.json`](final/metrics.json)
- [`final/def/chip_top.def`](final/def/chip_top.def)
- [`final/odb/chip_top.odb`](final/odb/chip_top.odb)
- [`librelane/config.yaml`](librelane/config.yaml)
- [`src/kianv_harris_edition/associative_cache.v`](src/kianv_harris_edition/associative_cache.v)
- [`src/cache_sram_I$.v`](src/cache_sram_I$.v)
