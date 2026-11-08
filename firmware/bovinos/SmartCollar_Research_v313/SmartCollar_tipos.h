// TIPOS PERSISTIDOS: alteracoes de layout exigem migracao/versionamento dos registros.
// Alguns tipos sao gravados como bytes; preservar tamanho e alinhamento ao alterar o formato.
// Tipos do SmartCollar v3.1 em arquivo separado: o Arduino gera prototipos de funcoes
// ANTES da primeira funcao do .ino; structs usadas como parametro precisam existir antes disso.
#pragma once
#include <Arduino.h>

enum Cand : uint8_t { C_PARADO = 0, C_LOCOMOCAO, C_CABECA_BAIXA, C_RITMO, C_OUTRO, N_CAND };

// Amostra crua vinda da tarefa do sensor (fila FreeRTOS)
struct Amostra { int16_t r[6]; uint32_t t; uint8_t flags; };      // flags bit0 = lacuna antes
struct Leitura { int16_t r[6]; float ax, ay, az, gx, gy, gz; };
struct Med { uint32_t t; float acc, giro; uint8_t cand; };
struct Hora { uint32_t id, chave, epochIni;   // id = identificador unico do segmento horario
              uint16_t seg[N_CAND], segEst[3], cabecaBaixaS; uint32_t passos; uint16_t impactos, validos; };
struct HoraCheckpoint { uint32_t magic, sessao, seq, soma; Hora h; };   // soma = FNV-1a com soma=0
struct MudancaRotulo { uint32_t t; uint8_t r; };
struct LinhaPend { uint8_t arq; char txt[230]; };

// Bruto binario. flags: bit0 = lacuna antes; bit1 = MARCADOR DE RELOGIO (v[0..3] = epoch_ms uint64)
// RawCab versao 3: ms64Inicio permite desembrulhar o ms de 32 bits dos registros (virada aos 49,7 dias)
struct __attribute__((packed)) RawCab { char magic[4]; uint16_t versao, hz; uint32_t sessao; float accLsb, gyrLsb; int64_t offsetMs; uint8_t relogio; char animal[16]; float bias[3]; uint64_t ms64Inicio; };
struct __attribute__((packed)) RawReg { uint32_t ms; int16_t v[6]; uint8_t rotulo, flags; };

// JSON simples sem alocacao dinamica
struct Json {
  char *b; size_t cap, n; bool primeiro;
  Json(char *buf, size_t c) : b(buf), cap(c), n(0), primeiro(true) { b[0] = 0; put("{"); }
  void put(const char *s) { size_t l = strlen(s); if (n + l < cap) { memcpy(b + n, s, l); n += l; b[n] = 0; } }
  void key(const char *k) { put(primeiro ? "\"" : ",\""); primeiro = false; put(k); put("\":"); }
  void u(const char *k, unsigned long long v) { char t[24]; snprintf(t, sizeof(t), "%llu", v); key(k); put(t); }
  void i(const char *k, long v) { char t[16]; snprintf(t, sizeof(t), "%ld", v); key(k); put(t); }
  void f(const char *k, float v, int d) { char t[24]; if (!isfinite(v)) strcpy(t, "null"); else snprintf(t, sizeof(t), "%.*f", d, (double)v); key(k); put(t); }
  void s(const char *k, const char *v) { key(k); put("\""); put(v); put("\""); }   // v: constantes internas
  void bo(const char *k, bool v) { key(k); put(v ? "true" : "false"); }
  void nul(const char *k) { key(k); put("null"); }
  const char *fim() { put("}"); return b; }
};
