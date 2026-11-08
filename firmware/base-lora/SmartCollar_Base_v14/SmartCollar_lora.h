// PROTOCOLO LoRa v5: estruturas compartilhadas entre colar bovino e Base v1.4.
// Alterar layout/tamanho exige versionar protocolo e atualizar os dois dispositivos.
// Estes pacotes nao oferecem autenticacao criptografica; usar somente em ambiente de teste autorizado.
// Protocolo LoRa SmartCollar v4 - MESMO arquivo no colar e na estacao-base.
#pragma once
#include <Arduino.h>
#include <RadioLib.h>

#define SC_LORA_VERSAO 5
// Colar -> base. 56 bytes: ~0,35 s no ar em SF9/125 kHz/CR 4/5 (abaixo de 400 ms).
// v5: contadores ACUMULADOS desde o boot (segundos por comportamento e passos). A base calcula cada
// intervalo pela diferenca entre dois pacotes recebidos -> pacotes perdidos nao perdem dados agregados.
// boot_id muda a cada inicializacao: separa "reiniciou" de "retransmitiu".
struct __attribute__((packed)) PacoteLoRa {
  uint8_t magic, versao; char animal[10]; uint16_t seq; uint32_t boot_id; uint16_t bat_mv; int8_t temp_c;
  uint8_t estado, candidato; uint16_t passos_hora; uint8_t indice_x10, flags; uint32_t uptime_s;
  uint32_t passos_total; uint32_t seg_total[5]; uint8_t tentativa;
};
// Base -> colar (confirmacao)
struct __attribute__((packed)) AckLoRa { uint8_t magic, versao; char animal[10]; uint16_t seq; int8_t rssi_base; };
static_assert(sizeof(PacoteLoRa) == 56, "PacoteLoRa mudou de tamanho: atualize colar E base");
static_assert(sizeof(AckLoRa) == 15, "AckLoRa mudou de tamanho");

// Compatibilidade RadioLib 7.x (begin com parametros) e 8.x (ConfigLoRa_t).
// Heltec V3: TCXO 1,8 V. SF9, BW 125 kHz, CR 4/5, sync 0x12, preambulo 8.
inline int16_t loraBeginHeltec(SX1262 &r, float mhz, int8_t dbm) {
#if defined(RADIOLIB_VERSION_MAJOR) && (RADIOLIB_VERSION_MAJOR >= 8)
  r.tcxoVoltage = RadioLibTCXOVoltage_t::Voltage1V8; r.useRegulatorLDO = false;
  ConfigLoRa_t c; c.frequency = RADIOLIB_UNIT_MEGA_FLT(mhz); c.bandwidth = RADIOLIB_UNIT_KILO(125);
  c.spreadingFactor = 9; c.codingRate = 5; c.syncWord = 0x12; c.power = dbm; c.preambleLength = 8;
  return r.begin(c);
#else
  return r.begin(mhz, 125.0f, 9, 5, 0x12, dbm, 8, 1.8f, false);
#endif
}
