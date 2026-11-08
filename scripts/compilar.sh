#!/usr/bin/env bash
# Uso: bash scripts/compilar.sh {bovinos|base-lora|chihuahua} {0|1} {0|1}
# LoRa 0|1, debug 0|1. Compila copia temporaria; preserva fontes do repo.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROLE="${1:?Passe papel: bovinos, base-lora ou chihuahua}"
LORA="${2:-0}"
DEBUG="${3:-0}"
case "$ROLE" in
  bovinos) NAME=SmartCollar_Research_v313;;
  base-lora) NAME=SmartCollar_Base_v14;;
  chihuahua) NAME=ChihuahuaLab_v15;;
  *) echo "Firmware desconhecido: $ROLE" >&2; exit 2;;
esac
case "$LORA:$DEBUG" in
  0:0|0:1|1:0|1:1) ;; *) echo 'LORA e DEBUG devem ser 0 ou 1' >&2; exit 2;;
esac
SRC="$ROOT/firmware/$ROLE/$NAME"
DEST="$ROOT/.build/$ROLE-LORA${LORA}-DEBUG${DEBUG}"
rm -rf "$DEST"
mkdir -p "$DEST/sketch" "$DEST/bin"
cp -R "$SRC/." "$DEST/sketch/"
# Arduino exige que pasta e .ino tenham o mesmo nome
mkdir -p "$DEST/$NAME"
cp -R "$DEST/sketch/." "$DEST/$NAME/"
rm -rf "$DEST/sketch"
INO="$DEST/$NAME/$NAME.ino"
if [[ "$LORA" == 1 ]]; then
 sed -i -e 's/#define HABILITAR_LORA 0/#define HABILITAR_LORA 1/' -e 's/#define HABILITAR_LORA_TX 0/#define HABILITAR_LORA_TX 1/' "$INO"
fi
if [[ "$DEBUG" == 1 ]]; then
 sed -i -e 's/#define HABILITAR_DEBUG 0/#define HABILITAR_DEBUG 1/' -e 's/#define TESTE_HORA_CURTA 0/#define TESTE_HORA_CURTA 1/' "$INO"
fi
case "$ROLE" in
  bovinos) CONFIG=SmartCollar_config_local.h;;
  base-lora) CONFIG=Base_config_local.h;;
  chihuahua) CONFIG=Chihuahua_config_local.h;;
esac
if [[ ! -f "$DEST/$NAME/$CONFIG" ]]; then echo "Falta $SRC/$CONFIG. Copie e configure o arquivo .example.h" >&2; exit 3; fi
LOG="$DEST/build.log"
{
 echo "=== Firmware: $ROLE LoRa=$LORA Debug=$DEBUG ==="
 echo "=== $(arduino-cli version) ==="
 arduino-cli core list
 arduino-cli lib list
 echo '=== Hashes das fontes compiladas ==='
 (cd "$DEST/$NAME" && sha256sum ./*)
 echo '=== Opcao --warnings all ==='
 arduino-cli compile -b esp32:esp32:heltec_wifi_lora_32_V3 --warnings all --verbose --output-dir "$DEST/bin" "$DEST/$NAME"
} > "$LOG" 2>&1 || { echo "Falha na compilacao. Consulte: $LOG" >&2; exit 1; }
echo "Build OK: $LOG"
