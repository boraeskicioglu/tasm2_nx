#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
export PATH="$PATH:/c/Program Files/Docker/Docker/resources/bin"
export MSYS_NO_PATHCONV=1
if [ -z "${DCR_LIBNX32:-}" ] && [ -f /f/spider_total_mayhem_nx/libnx32/prefix/lib/libnx.a ]; then
  export DCR_LIBNX32=/f/spider_total_mayhem_nx/libnx32/prefix
fi
exec "$HERE/runtime/tools/docker_build.sh" "$@"
