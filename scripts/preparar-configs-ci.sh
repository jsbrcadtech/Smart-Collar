#!/usr/bin/env bash
# Somente CI: senhas ficticias; nao usar binaries resultantes em campo.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for pasta in "$ROOT"/firmware/*/*; do
  for ex in "$pasta"/*_config_local.example.h; do
    [[ -e "$ex" ]] || continue
    target="${ex/.example.h/.h}"
    sed 's/#define SMARTCOLLAR_CONFIG_PRONTA 0/#define SMARTCOLLAR_CONFIG_PRONTA 1/' "$ex" > "$target"
  done
done
