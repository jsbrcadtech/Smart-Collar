# Arquitetura

```text
MPU6050 (I2C GPIO4/5)
       |
       v
Heltec bovinos ESP32-S3
  | FIFO MPU + tarefa de aquisição (20 Hz)
  | fila RAM -> análise + rotulagem temporal
  | LittleFS / NVS: amostras, resumos, eventos, checkpoints
  | OLED + AP Wi-Fi + painel web
  +-- SX1262 LoRa v5 (opcional) ------> Heltec Base v1.4
                                          | valida seq/boot_id
                                          | grava base.csv com checksum
                                          | responde ACK
                                          +-- AP Wi-Fi + painel web

Chihuahua Lab v1.5 -> firmware separado, não compartilha o formato LoRa da base bovina.
```

A configuração padrão mantém LoRa desligado no colar; habilitar exige alterar `HABILITAR_LORA` e instalar antenas adequadas. O protocolo v5 é compartilhado via cópias iguais de `SmartCollar_lora.h` nos dois sketches; qualquer mudança de formato precisa atualizar ambos, com revisão e testes.

**Candidatos de comportamento não validados:** PARADO, LOCOMOCAO, CABECA_BAIXA, RITMO_MASTIGACAO, OUTRO_ATIVO. Rotulagem por observador é separada dos candidatos calculados.
