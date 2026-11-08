# Preparação e compilação

## Versões de referência do pacote de origem

- Placa: `esp32:esp32:heltec_wifi_lora_32_V3` — Heltec WiFi LoRa 32(V3)
- Core ESP32: **3.3.12**
- U8g2: **2.37.1**
- RadioLib: **8.0.0** (a origem também documenta **7.8.1**)

**Importante:** as senhas originalmente embutidas foram movidas para cabeçalhos `*_config_local.h` ignorados pelo Git. Os SHA-256 e logs de origem se referem ao código anterior a essa mudança e, portanto, **não atestam** o build do presente repositório. Os próximos builds devem ser gerados pelo CI ou localmente.

## Configuração local

No diretório de **cada** sketch que for compilar:

1. Duplique `NOME_config_local.example.h` para `NOME_config_local.h` (retire `.example` do nome).
2. Substitua os valores `SUBSTITUIR_...` por senhas fortes únicas, não reutilizadas.
3. No arquivo **local**, mude `#define SMARTCOLLAR_CONFIG_PRONTA 0` para `1`.
4. Nunca envie o arquivo local ao GitHub (`.gitignore` o protege contra commits normais).

## Arduino IDE

1. Instale a plataforma ESP32 3.3.12 e a placa Heltec WiFi LoRa 32(V3).
2. Instale U8g2 2.37.1. Se habilitar LoRa, instale RadioLib 8.0.0 ou 7.8.1.
3. Abra o `.ino` na pasta do próprio sketch (nome da pasta deve coincidir com o `.ino`).
4. Para bovinos com base, ative `#define HABILITAR_LORA 1` no colar e use o firmware Base v1.4 com protocolo v5; conecte antenas em **ambas** as placas.
5. Selecione partição de flash que inclua espaço de sistema de arquivos LittleFS e compile. Se houver erro, registre log de build antes de modificar código.

## Arduino CLI

```bash
arduino-cli config init --overwrite
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
arduino-cli lib install 'U8g2@2.37.1'
arduino-cli lib install 'RadioLib@8.0.0'
# Após configurar os três *_config_local.h:
bash scripts/compilar.sh bovinos 0 0
bash scripts/compilar.sh bovinos 1 0
bash scripts/compilar.sh base-lora 1 0
bash scripts/compilar.sh chihuahua 0 0
```

Use `bash scripts/compilar.sh bovinos 1 1` para LoRa+debug juntos **na bancada**. O script compila numa cópia temporária sem alterar os fontes, usa `--warnings all`, gera logs e binários em `.build/` (ignorado pelo Git). A compilação com flag `debug=1` também ativa `TESTE_HORA_CURTA` por padrão; não grave essa configuração em um animal.

## Compilação automática

O GitHub Actions (`.github/workflows/compile.yml`) prepara **somente** senhas fictícias no runner e faz compilações de referência. Nenhuma credencial real é usada ou carregada no CI. Os artefatos do CI não devem ser gravados diretamente em animais sem configurar os dados locais e validar a placa.

> Não confundir compilação com validação de dados, segurança física ou prontidão de uso em campo.
