#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI_DIR="$ROOT_DIR/.tools/bin"
INSTALLER="$ROOT_DIR/.tools/install-arduino-cli.sh"
CONFIG="$ROOT_DIR/arduino-cli.yaml"

mkdir -p "$CLI_DIR" "$ROOT_DIR/.arduino15" "$ROOT_DIR/.arduino-cache" "$ROOT_DIR/Arduino/libraries"

if [[ ! -x "$CLI_DIR/arduino-cli" ]]; then
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh -o "$INSTALLER"
  BINDIR="$CLI_DIR" sh "$INSTALLER"
fi

"$CLI_DIR/arduino-cli" --config-file "$CONFIG" core update-index
"$CLI_DIR/arduino-cli" --config-file "$CONFIG" core install esp32:esp32
"$CLI_DIR/arduino-cli" --config-file "$CONFIG" lib install ESP32Servo
"$CLI_DIR/arduino-cli" --config-file "$CONFIG" lib install AccelStepper

echo "Arduino CLI pronto em $CLI_DIR/arduino-cli"
