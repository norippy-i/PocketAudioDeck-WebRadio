#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$PROJECT_DIR/.pio-core}"
if [[ -d "$PROJECT_DIR/.pio-python-deps" ]]; then
  export PYTHONPATH="$PROJECT_DIR/.pio-python-deps"
fi
export PYTHONNOUSERSITE=1

exec platformio "$@"
