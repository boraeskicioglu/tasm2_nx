#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
export PATH="$PATH:/c/Program Files/Docker/Docker/resources/bin"
export MSYS_NO_PATHCONV=1
LAUNCHER_DIR="$HERE" PAYLOAD=tasm2_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
