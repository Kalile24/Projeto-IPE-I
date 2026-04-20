#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI="$ROOT_DIR/.tools/bin/arduino-cli"
CONFIG="$ROOT_DIR/arduino-cli.yaml"
BUILD_SKETCH_DIR="$ROOT_DIR/.build/sketch"
OUTPUT_DIR="$ROOT_DIR/wokwi/build"

if [[ ! -x "$CLI" ]]; then
  if command -v arduino-cli >/dev/null 2>&1; then
    CLI="$(command -v arduino-cli)"
  else
    echo "arduino-cli nao encontrado." >&2
    echo "Rode tools/setup-arduino-cli.sh ou instale arduino-cli no PATH." >&2
    exit 1
  fi
fi

mkdir -p "$BUILD_SKETCH_DIR" "$OUTPUT_DIR"
cp "$ROOT_DIR/wokwi/sketch.ino" "$BUILD_SKETCH_DIR/sketch.ino"

"$CLI" --config-file "$CONFIG" compile \
  --fqbn esp32:esp32:esp32 \
  --output-dir "$OUTPUT_DIR" \
  "$BUILD_SKETCH_DIR"
