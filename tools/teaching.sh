#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
image=minios-teaching:local
if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t "$image" -f "$repo/tools/teaching/Dockerfile" "$repo/tools/teaching"
fi
args=(run --rm --platform linux/amd64 --mount "type=bind,src=$repo,dst=/work" -w /work)
if [[ -t 0 && -t 1 ]]; then args+=(-it); else args+=(-i); fi
if [[ $# -eq 0 ]]; then set -- test-teaching; fi
exec docker "${args[@]}" "$image" make "$@"
