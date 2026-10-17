#!/bin/sh
# Runs heavy work (a build, a sanitizer suite, a long GPU test, a bench) one at a time across photo-beauty
# worktrees, once 4 GB of RAM is free: the WSL VM has 15 GB, shared with other projects' builds.
# Usage: tools/heavy.sh cmake --build build/default
if [ -z "$PB_HEAVY_LOCKED" ]; then
  export PB_HEAVY_LOCKED=1
  exec flock "$(cd "$(dirname "$0")" && cd "$(git rev-parse --git-common-dir)" && pwd)/heavy.lock" "$0" "$@"  # from any cwd
fi
until awk '/MemAvailable/ { exit !($2 > 4 * 1048576) }' /proc/meminfo; do sleep 5; done
export CCACHE_DIR="$HOME/.cache/ccache-pb" CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
exec "$@"
