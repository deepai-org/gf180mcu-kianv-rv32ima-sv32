#!/usr/bin/env bash
set -euo pipefail

image=${KIANV_LINUX_BUILD_IMAGE:-kianv-linux-builder:ubuntu-24.04}
container_platform=${CONTAINER_PLATFORM:-linux/arm64}
container_cpus=${CONTAINER_CPUS:-16}
container_memory=${CONTAINER_MEMORY:-32g}
flow_timeout=${FLOW_TIMEOUT:-4h}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(git -C "$script_dir" rev-parse --show-toplevel)

for command_name in docker timeout; do
  command -v "$command_name" >/dev/null 2>&1 || {
    echo "error: required host command is missing: $command_name" >&2
    exit 2
  }
done

timeout --kill-after=30s 20m docker build \
  --platform "$container_platform" -t "$image" "$script_dir"

timeout --kill-after=2m "$flow_timeout" docker run --rm \
  --name kianv-linux-build \
  --platform "$container_platform" \
  --user "$(id -u):$(id -g)" \
  --cpus "$container_cpus" \
  --memory "$container_memory" \
  --memory-swap "$container_memory" \
  --pids-limit 4096 \
  --security-opt no-new-privileges \
  --mount "type=bind,source=$repo_root,target=/work" \
  --workdir /work/sim/linux \
  "$image" ./build-linux.sh
