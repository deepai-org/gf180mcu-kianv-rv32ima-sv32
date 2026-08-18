#!/usr/bin/env bash
set -euo pipefail

IMAGE="${NIX_IMAGE:-nixos/nix@sha256:76ddc151e8f341c98afe8b9ac022f703d3f6e96ffeed6dad7a23ee3bff5cff44}"
PLATFORM="${CONTAINER_PLATFORM:-linux/arm64}"
NIX_VOLUME="${NIX_VOLUME:-kianv-gf180-nix-2-24-11}"
ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
HOST_UID=$(id -u)
HOST_GID=$(id -g)
MIN_FREE_GIB=${MIN_FREE_GIB:-100}
MIN_MEMORY_GIB=${MIN_MEMORY_GIB:-16}
HEAVY_JOB_LOCK=${HEAVY_JOB_LOCK:-/tmp/kianv-gf180-heavy-job.lock}

for command_name in awk df docker flock timeout; do
  command -v "$command_name" >/dev/null 2>&1 || {
    echo "error: required host command is missing: $command_name" >&2
    exit 2
  }
done

free_kib() { df -Pk "$1" | awk 'NR == 2 {print $4}'; }
required_kib=$(( MIN_FREE_GIB * 1024 * 1024 ))
docker_root=$(docker info --format '{{.DockerRootDir}}')
for path in "$ROOT" "$docker_root"; do
  if (( $(free_kib "$path") < required_kib )); then
    echo "error: refusing below $MIN_FREE_GIB GiB free at $path" >&2
    exit 2
  fi
done

if (( $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo) < MIN_MEMORY_GIB * 1024 * 1024 )); then
  echo "error: refusing below $MIN_MEMORY_GIB GiB available memory" >&2
  exit 2
fi

if (( $# == 0 )); then
  set -- all
fi

exec 9>"$HEAVY_JOB_LOCK"
flock -n 9 || {
  echo "error: another heavy job holds $HEAVY_JOB_LOCK" >&2
  exit 2
}

docker pull --platform "$PLATFORM" "$IMAGE" >/dev/null
actual_architecture=$(docker image inspect "$IMAGE" --format '{{.Architecture}}')
expected_architecture=${PLATFORM#linux/}
if [[ "$actual_architecture" != "$expected_architecture" ]]; then
  echo "error: image architecture is $actual_architecture, expected $expected_architecture" >&2
  exit 2
fi

timeout --kill-after=2m "${FLOW_TIMEOUT:-24h}" docker run --rm \
  --name "${CONTAINER_NAME:-kianv-gf180-reproduction}" \
  --platform "$PLATFORM" \
  --cpus "${CONTAINER_CPUS:-28}" \
  --memory "${CONTAINER_MEMORY:-96g}" \
  --memory-swap "${CONTAINER_MEMORY_SWAP:-104g}" \
  --pids-limit "${CONTAINER_PIDS:-8192}" \
  --security-opt no-new-privileges \
  --mount "type=volume,source=$NIX_VOLUME,target=/nix" \
  --mount "type=bind,source=$ROOT,target=/work" \
  --workdir /work \
  --env "HOST_UID=$HOST_UID" \
  --env "HOST_GID=$HOST_GID" \
  --env "MIN_FREE_GIB=$MIN_FREE_GIB" \
  --env "MIN_MEMORY_GIB=$MIN_MEMORY_GIB" \
  --env "XOR_THREADS=${XOR_THREADS:-28}" \
  --entrypoint /bin/sh \
  "$IMAGE" -lc '
    set -e
    status=0
    git config --global --add safe.directory /work
    git config --global --add safe.directory /work/gf180mcu
    nix develop --extra-experimental-features "nix-command flakes" \
      --accept-flake-config --command bash reproduce/run.sh "$@" || status=$?
    for path in gf180mcu librelane/runs final img cocotb/sim_build; do
      if [ -e "$path" ]; then chown -R "$HOST_UID:$HOST_GID" "$path"; fi
    done
    exit "$status"
  ' -- "$@"
