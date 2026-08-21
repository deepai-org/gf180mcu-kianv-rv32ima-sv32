# GF180 Run-1 GDS reproduction

This directory reproduces the GDS committed in the
`GF180MCU_Tapeout_Dec2025` tag. The tag points to commit
`1892792dd2e77df37443cabf0e69320b08492827`; its parent contains the final RTL
and flow configuration, and the tagged commit adds the submitted GDS.

## Requirements

- A 64-bit ARM Linux host
- Git and Docker, plus the ordinary GNU/Linux `awk`, `df`, `flock`, and
  `timeout` commands
- At least 100 GiB free on both the repository and Docker-storage filesystems
- At least 16 GiB RAM; 32 GiB or more is recommended

The wrapper pins the ARM64 image manifest for Nix 2.24.11. The taped-out
`flake.lock` still pins LibreLane and all EDA dependencies. Set both
`CONTAINER_PLATFORM` and `NIX_IMAGE` to use another Docker platform.

## Clean-machine quick start

```sh
git clone --branch reproduce-gf180-run1 \
  https://github.com/deepai-org/gf180mcu-kianv-rv32ima-sv32.git
cd gf180mcu-kianv-rv32ima-sv32
./reproduce/run-in-docker.sh preflight
./reproduce/run-in-docker.sh all
```

`all` runs preflight, downloads the pinned PDK, generates the GDS, runs the
configured sign-off steps, copies the final views, and compares the result to
the submitted GDS. On ARM64, the known geometric difference is reported but
does not make `all` fail. Other flow and verification failures still return a
nonzero status.

The recorded run took approximately 5 hours 22 minutes with a limit of 28 CPUs
and 96 GiB RAM. Initial Docker, Nix, and PDK downloads add time and depend on
the network connection.

## Individual stages

| Command argument | Action | Successful exit status |
| --- | --- | ---: |
| `preflight` | Check commits, protected inputs, resources, tools, and reference hashes | 0 |
| `loom-preflight` | Check Loom hashes, interface specialization, and all 21 post-Yosys SRAM paths | 0 |
| `pdk` | Run preflight and acquire the pinned PDK | 0 |
| `gds` | Run preflight, acquire the PDK, execute LibreLane, and copy final views | 0 |
| `loom-gds` | Run Loom preflight and LibreLane, then build and verify the hashed final-view bundle | 0 |
| `loom-archive` | Rebuild and verify the bundle for the latest completed Loom run | 0 |
| `verify` | Check the generated hash and run XOR if it differs | 0 if identical; 2 if geometrically different |
| `compare` | Run the reference-versus-generated geometric XOR directly | 0 if identical; 2 if different |
| `all` | Run `preflight`, `gds`, and `verify` as the normal end-to-end workflow | 0 after a completed flow, including the documented ARM64 mismatch |
| `loom-all` | Run the complete pinned Loom physical flow and hash its auditable release bundle | 0 after a completed, output-producing flow |
| `sim` | Run the upstream tapeout testbench; currently expected to fail during elaboration | Nonzero currently |

The `sim` stage is deliberately not part of the quick start or `all`. Its
source list is incomplete, and upstream had disabled it in the tapeout CI. It
is not required to reproduce the submitted physical artifact.

The Loom conversion has a separate fabrication path because it intentionally
differs from the submitted upstream RTL and cannot pass the byte-identical
tapeout-input check used by `preflight`:

```sh
./reproduce/run-in-docker.sh loom-preflight
./reproduce/run-in-docker.sh loom-all
```

The Loom preflight also applies a hash-checked temporary script overlay so the
pinned LibreLane synthesis implementation honors its own
`SYNTH_SHARE_RESOURCES: false` setting. The Nix store remains unchanged.
It requires a clean, committed worktree so the release provenance also binds
the top-level RTL, constraints, slot configuration, and runner/tool inputs.

See [`../LOOM_PHYSICAL_FLOW.md`](../LOOM_PHYSICAL_FLOW.md) for artifact
provenance, regeneration, and the required fabrication-release review. See
[`LOOM_RESULTS.md`](LOOM_RESULTS.md) for complete-run evidence and comparisons.

## Expected result

A successful ARM64 run ends with `all stages completed successfully`, creates
`final/gds/chip_top.gds`, and reports that its geometry differs from the
submitted reference. The recorded hashes are:

```text
09dd61160c252740fa5cb7efabcc98ddd5f0a179b436b7f2302e445f2f1cca3d  submitted chip_top.gds
71f97369a2d51506ba7f42b1910ef8f548246d8efd94fba199056d2f91c27a7c  recorded ARM64 chip_top.gds
```

The recorded comparison found 13,936,919 XOR polygons. A different generated
hash or XOR count is not automatically a physical-verification failure; use
the run reports and sign-off results to judge a new implementation. A zero XOR
count is the strongest reproduction result.

Outputs are located at:

- `final/gds/chip_top.gds`: final sealed and filled GDS
- `final/metrics.json`: consolidated LibreLane metrics
- `final/spef/`: extracted min, nominal, and max parasitic views
- `final/signoff/`: selected timing, IR, DRC, antenna, LVS, and manufacturability reports
- `final/RELEASE_PROVENANCE.json`: run, source, PDK, and tool identity
- `final/SHA256SUMS`: verified hashes for the complete final-view bundle
- `final/`: copied DEF, ODB, netlist, SDC, and other final views
- `librelane/runs/RUN_*`: complete step logs and reports

The Nix store is retained in the Docker volume
`kianv-gf180-nix-2-24-11`, so interrupted runs do not redownload the toolchain.
LibreLane outputs remain under `librelane/runs/`; `final/` contains the copied
sign-off outputs.

The host wrapper permits one heavy job at a time and limits the container to
28 CPUs, 96 GiB RAM, 104 GiB RAM plus swap, 8192 processes, and 24 hours.
Override these with `CONTAINER_CPUS`, `CONTAINER_MEMORY`,
`CONTAINER_MEMORY_SWAP`, `CONTAINER_PIDS`, and `FLOW_TIMEOUT`. On a shared EDA
host, set `HEAVY_JOB_LOCK` to the host's common heavy-job lock path.

The preflight stage refuses to proceed if tracked tapeout inputs differ from
the tag or disk/RAM headroom is below the safety thresholds. Override only the
resource thresholds with `MIN_FREE_GIB` and `MIN_MEMORY_GIB`.

## Troubleshooting and resumption

- **Interrupted run:** rerun `./reproduce/run-in-docker.sh all`. The Docker Nix
  volume retains downloaded tools. LibreLane normally starts a new `RUN_*`
  directory rather than resuming the interrupted placement-and-routing step;
  existing run directories are left intact for diagnosis.
- **Insufficient repository disk:** inspect `librelane/runs/` and `final/`.
  Archive or remove only run directories that you have positively identified
  as stale. A complete run can consume tens of GiB.
- **Insufficient Docker disk:** check `docker info --format
  '{{.DockerRootDir}}'` and Docker's own disk-usage tools. The named volume
  `kianv-gf180-nix-2-24-11` is the reusable tool cache; deleting it forces a
  complete toolchain download on the next run.
- **Architecture mismatch:** the default image is pinned specifically for
  `linux/arm64`. Running another architecture requires setting both
  `CONTAINER_PLATFORM` and `NIX_IMAGE` to a matching manifest; changing only
  one is rejected.
- **Heavy-job lock:** if no reproduction is active but the wrapper reports the
  lock as held, check for a surviving wrapper process before changing
  `HEAVY_JOB_LOCK`. The lock itself is released automatically when its process
  exits.
- **Expected XOR status:** direct `verify` and `compare` calls return status 2
  for a geometric mismatch. The normal `all` command treats the documented
  ARM64 mismatch as a completed run and returns 0.

## Reference

The committed reference has these SHA-256 values:

```text
d149a25bff5523c51018f0ba2bba006e7491e25d19fc7ac8b5175825cedc8a58  chip_top.gds.zip
09dd61160c252740fa5cb7efabcc98ddd5f0a179b436b7f2302e445f2f1cca3d  chip_top.gds
```

A byte-for-byte match is the strongest result. If the hashes differ, `verify`
automatically extracts the submitted GDS to a temporary directory and runs the
pinned KLayout layer-by-layer XOR. `compare` runs that geometric check directly.
Temporary GDS, XML, and log files are removed when the comparison exits.

The upstream tapeout tag publishes the submitted GDS but not its original
`metrics.json`, ODB, DEF, SPEF, or STA reports. Consequently, the reproduction
can compare final geometry but cannot directly compare upstream and ARM64
timing, slew, or capacitance statistics.

See [RESULTS.md](RESULTS.md) for the recorded native ARM64 reproduction run,
including sign-off results and the reference XOR outcome.
