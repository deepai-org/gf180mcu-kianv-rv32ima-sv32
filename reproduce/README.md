# GF180 Run-1 GDS reproduction

This directory reproduces the GDS committed in the
`GF180MCU_Tapeout_Dec2025` tag. The tag points to commit
`1892792dd2e77df37443cabf0e69320b08492827`; its parent contains the final RTL
and flow configuration, and the tagged commit adds the submitted GDS.

## Requirements

- Linux
- Docker with at least 100 GiB free on both the repository and Docker-storage
  filesystems
- At least 16 GiB RAM; 32 GiB or more is recommended

The wrapper pins the ARM64 image manifest for Nix 2.24.11. The taped-out
`flake.lock` still pins LibreLane and all EDA dependencies. Set both
`CONTAINER_PLATFORM` and `NIX_IMAGE` to use another Docker platform.

## Run

```sh
./reproduce/run-in-docker.sh preflight
./reproduce/run-in-docker.sh pdk
./reproduce/run-in-docker.sh sim
./reproduce/run-in-docker.sh gds
./reproduce/run-in-docker.sh verify
./reproduce/run-in-docker.sh compare
```

`all` runs preflight, PDK acquisition, GDS generation, and verification. The
`sim` stage exposes the tapeout tag's original testbench separately; it was
disabled in the upstream tapeout CI and currently fails to elaborate because
its source list is incomplete. It is not a prerequisite for reproducing the
submitted physical artifact.

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

See [RESULTS.md](RESULTS.md) for the recorded native ARM64 reproduction run,
including sign-off results and the reference XOR outcome.
