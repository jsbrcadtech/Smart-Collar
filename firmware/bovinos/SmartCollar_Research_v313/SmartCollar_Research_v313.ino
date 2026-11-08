/*
  SMART COLLAR RESEARCH v3.1.3 - coleta para pesquisa (bovinos)
  Hardware: Heltec WiFi LoRa 32 V3.2 (ESP32-S3 + SX1262) + GY-521 MPU6050
  Ligacoes IGUAIS: MPU VCC->3V3, GND->GND, SDA->GPIO4, SCL->GPIO5.
  Bibliotecas: U8g2 (olikraus). Opcional: RadioLib 7.x ou 8.x (jgromes) se HABILITAR_LORA=1.
  Placa: "Heltec WiFi LoRa 32(V3)". Partition Scheme com particao de arquivos (SPIFFS/LittleFS).
  COMPILADO de verdade com arduino-esp32 3.3.12 + U8g2 2.37.1 + RadioLib 7.8.1 e 8.0.0.

  v3.1.3 (quarta revisao - consistencia ENTRE arquivos):
   k. Relogio unico de 64 bits (ms64/ms32); nenhuma comparacao depende de ms32() < 2^31. O bug do
      atraso de debug (fila parava apos 24,85 dias MESMO com debug desligado) foi removido: sem
      HABILITAR_DEBUG o processamento e incondicional. TESTE_OFFSET_MS adianta o relogio para testar
      24,85 / 49,7 dias na bancada em 1 minuto.
   l. Cada segmento horario tem ID unico (seg_id). Recuperacao elimina duplicatas pelo ID; horas nao
      gravadas vao para uma fila DURAVEL na NVS (particao separada: funciona com a flash de arquivos
      cheia), ate 24 horas, regravadas e confirmadas uma a uma. horas.csv nunca passa pela fila de
      download (se estiver sendo baixado, a hora vai para a fila duravel).
   m. Marcadores de relogio do bruto entram NA ORDEM das amostras (linha do tempo), nao no instante
      do clique; exportacao aplica o 1o marcador tambem as amostras anteriores e sinaliza qualquer
      carimbo fora de ordem (coluna fora_de_ordem).
   n. Testes de bancada (HABILITAR_DEBUG 1): /api/debug/atraso, /api/debug/flash_cheia,
      TESTE_HORA_CURTA (uma "hora" = 2 min) para cortar energia em viradas de hora.

  v3.1.2 (terceira revisao - integridade dos dados):
   g. Hora e resumo agregados pelo HORARIO DA AMOSTRA (nao pelo instante do processamento): atraso da
      fila na virada da hora nao joga amostras das 11:59 na hora das 12:00.
   h. Checkpoint em DOIS slots alternados com sequencia + checksum, verificado apos gravar. O
      checkpoint so e apagado depois de CONFIRMADA a linha no horas.csv; se falhar, a hora fica
      pendente em RAM e e regravada a cada 60 s (evento HORA_NAO_GRAVADA). horas.csv tem reserva
      de espaco menor que os outros arquivos (prioridade).
   i. Bruto: fim da captura pelo horario da amostra; "Parar" processa a fila antes de fechar;
      tempo em 64 bits (cabecalho v3 + desembrulho no download): sem virada aos 49,7 dias.
      CSVs (resumo, eventos, bruto) usam ms de 64 bits desde o boot.
   j. /api/debug/atraso so existe com HABILITAR_DEBUG 1 (desligado por padrao).

  v3.1.1 (segunda revisao):
   a. Rotulo humano aplicado pelo HORARIO da amostra (linha do tempo de rotulos), nao pela hora em que
      a fila foi processada: atraso da fila nao contamina mais os dados de treino.
   b. Checkpoint da hora a cada 60 s em arquivo + copia a cada 1 s na RAM do RTC (sobrevive a reset
      por software/watchdog). Hora retomada e regravada NA HORA (antes: 2o desligamento perdia tudo).
   c. LoRa v5: contadores ACUMULADOS por comportamento + boot_id. A base reconstroi intervalos
      perdidos por diferenca; retransmissao e reinicio sao distinguidos pelo boot_id.
   d. Bateria: SEM_LEITURA / CRITICA (<3,3 V) / BAIXA / OK; tensao critica nao e mais descartada.
      Reset por brownout = "QUEDA_ALIMENTACAO" (pode ser bateria, cabo ou pico de consumo).
   e. Falha do MPU nao encerra a gravacao bruta: marca lacuna e continua quando o sensor volta.
   f. Teste de bancada: POST /api/debug/atraso s=N segura a fila por N s (max 50) para simular atraso.

  v3.1 (correcoes da revisao da v3.0):
   1. Hora corrente salva a cada 5 min (/hora_atual.bin): perda maxima ~5 min apos desligar.
      Relogio sobrevive a reset por watchdog/software (RTC interno); hora e retomada se for a mesma.
   2. Indice de atividade compara a MESMA hora do dia nos 7 dias anteriores (exige relogio).
   3. Tempo: marcadores de relogio DENTRO do bruto a cada sincronizacao; carimbo das amostras
      suavizado (deriva do oscilador do MPU acompanhada); relogio em 64 bits (sem virada de 49 dias).
   4. Tarefa FreeRTOS dedicada esvazia a FIFO do MPU a cada 100 ms para uma fila de 60 s:
      o loop (Wi-Fi, OLED, flash, downloads) pode travar ate ~60 s sem perder amostras.
   5. LoRa com confirmacao (ACK), ate 3 retransmissoes com espera aleatoria, recuperacao de radio
      travado e campos acumulados no pacote.
   6. Passos so contam em sequencia regular (>=3 picos com 0,35-1,5 s entre eles): mastigacao fraca,
      sacudidas isoladas e cabecadas unicas sao descartadas (contador de picos descartados).
   7. Pinos do OLED renomeados (a placa Heltec ja define SDA_OLED etc. -> v2.2/v3.0 NAO compilavam).

  IMPORTANTE: "candidatos" (passos, cabeca baixa, ritmo, alerta de atividade) sao heuristicas
  NAO validadas. Temperatura do MPU6050 = temperatura DO CHIP, nao do animal.
*/
#include "SmartCollar_tipos.h"
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <math.h>

// ======================= CONFIGURACAO =======================
#include "SmartCollar_config_local.h"  // Copie .example.h para este nome; nunca commite credenciais.
constexpr bool FORMATAR_FS_SE_FALHAR = false; // true apaga a flash de arquivos! So com backup.

#define HABILITAR_LORA 0          // 1 = envia resumo (exige RadioLib + ANTENA conectada)
#define HABILITAR_DEBUG 0         // 1 = endpoints /api/debug/* (SO bancada; manter 0 em campo)
#define TESTE_HORA_CURTA 0        // 1 (com DEBUG) = "hora" de 2 min para testar viradas de hora
#define TESTE_OFFSET_MS 0ULL      // ex.: 2147423648ULL = 24,85 dias - 60 s; 4294907296ULL = 49,7 dias - 60 s
constexpr float LORA_FREQ_MHZ = 915.0f; // Brasil 915-928 / Canada-EUA 902-928. Confira a variante da placa.
constexpr int8_t LORA_POTENCIA_DBM = 14;
constexpr uint32_t LORA_INTERVALO_S = 60;
constexpr uint32_t LORA_JANELA_ACK_MS = 2000; // base responde ~300 ms apos receber
constexpr uint8_t LORA_RETRIES = 3;

constexpr bool CPU_80MHZ = true;
constexpr uint32_t OLED_AUTO_DESLIGA_S = 300; // 0 = sempre ligada; botao PRG religa
constexpr uint32_t WIFI_AUTO_DESLIGA_MIN = 0; // 0 = nunca
constexpr uint32_t WDT_S = 30;
constexpr uint32_t CHECKPOINT_S = 60;         // arquivo a cada 60 s (RAM do RTC a cada 1 s)

// Amostragem
constexpr uint16_t FREQ_HZ = 20;
constexpr uint32_t PERIODO_MS = 1000 / FREQ_HZ;
constexpr uint8_t SMPLRT_DIV = (uint8_t)(1000 / FREQ_HZ - 1);
constexpr float ACC_LSB = 8192.0f;            // +-4 g
constexpr float GYR_LSB = 65.5f;              // +-500 graus/s
constexpr uint16_t FILA_AMOSTRAS = FREQ_HZ * 60; // 60 s de folga para o loop
constexpr uint16_t TAM_RITMO = FREQ_HZ * 10;
constexpr uint8_t RESUMO_S = 10;
constexpr int TAM_HIST = 300;
constexpr int TAM_HORAS = 240;
constexpr uint32_t RAW_MAX_MIN = 60;
constexpr size_t RESUMO_MAX_BYTES = 600000UL;
constexpr size_t HORAS_MAX_BYTES = 150000UL;

// Limiares (TODOS a validar com video)
constexpr float LIM_PARADO_ACC = .035f, LIM_PARADO_GIRO = 5.0f;
constexpr float LIM_INTENSO_ACC = .18f, LIM_INTENSO_GIRO = 45.0f;
constexpr float LIM_PASSO_ON = .08f, LIM_PASSO_OFF = .045f;
constexpr uint32_t PASSO_INT_MIN_MS = 350, PASSO_INT_MAX_MS = 1500; constexpr uint8_t PASSO_TREM_MIN = 3;
constexpr float RITMO_HZ_MIN = .6f, RITMO_HZ_MAX = 1.8f, RITMO_QUAL_MIN = .5f, RITMO_VAR_MIN = 2.0f;
constexpr float CABECA_SEM_REF_GRAUS = 35.0f, CABECA_FRACAO = .6f;
constexpr float IMPACTO_G = 2.5f;
constexpr float INDICE_ALERTA = 2.0f; constexpr int MIN_DIAS_BASE = 3;

// Pinos Heltec V3.2 (nomes proprios: a placa ja define SDA_OLED/SCL_OLED/RST_OLED)
constexpr uint8_t PIN_MPU_SDA = 4, PIN_MPU_SCL = 5;
constexpr uint8_t PIN_VBAT_ADC = 1, PIN_VBAT_EN = 37;
constexpr uint8_t PIN_OLED_PWR = 36, PIN_OLED_RST = 21, PIN_OLED_SDA = 17, PIN_OLED_SCL = 18;
constexpr uint8_t PIN_LED = 35, PIN_BTN = 0;

// ======================= ESTADO GLOBAL =======================
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, PIN_OLED_SCL, PIN_OLED_SDA, PIN_OLED_RST);
TwoWire scannerOLED(1);
WebServer web(80);
Preferences prefs;

const char *CAND_NOME[N_CAND] = {"PARADO", "LOCOMOCAO", "CABECA_BAIXA", "RITMO_MASTIGACAO", "OUTRO_ATIVO"};
const char *ROTULOS[] = {"SEM_ROTULO", "DEITADO", "EM_PE_PARADO", "CAMINHANDO", "PASTEJANDO",
                         "RUMINANDO_EM_PE", "RUMINANDO_DEITADO", "COCHO", "BEBENDO", "OUTRO"};
constexpr uint8_t N_ROTULOS = sizeof(ROTULOS) / sizeof(ROTULOS[0]);

uint8_t rotuloAtual = 0, estadoAtual = 3, candAtual = C_PARADO;
// Linha do tempo de rotulos: cada mudanca guarda o ms32() em que o observador clicou.
MudancaRotulo mudRot[32]; uint8_t mudRotQtd = 0; uint8_t rotuloProc = 0;   // rotuloProc = rotulo da amostra em processamento
uint32_t bootId = 0, segCandTotal[N_CAND] = {0, 0, 0, 0, 0};
RTC_NOINIT_ATTR HoraCheckpoint rtcCk;
uint32_t ckSeq = 0, ckFalhas = 0, horasNaoGravadas = 0, horasPerdidas = 0, ultimaTentPendente = 0, tJanela = 0, tUltimaAmostra = 0;
// Fila DURAVEL de horas nao gravadas (NVS) + IDs ja confirmados no horas.csv (para eliminar duplicatas)
constexpr int MAX_PEND = 24; Hora pend[MAX_PEND]; uint8_t pendQtd = 0;
constexpr int MAX_IDS = 64; uint32_t idsGravados[MAX_IDS]; uint8_t idsPos = 0, idsQtd = 0;
#if TESTE_HORA_CURTA && HABILITAR_DEBUG
constexpr uint64_t DURACAO_HORA_MS = 120000ULL;   // TESTE: "hora" de 2 minutos
#else
constexpr uint64_t DURACAO_HORA_MS = 3600000ULL;
#endif
#if HABILITAR_DEBUG
bool atrasoAtivo = false, flashCheiaTeste = false; uint32_t atrasoInicio = 0, atrasoDur = 0;
#endif
const char *CK_SLOT[2] = {"/ck_a.bin", "/ck_b.bin"};
constexpr uint32_t CK_MAGIC = 0x48523333;   // v3.1.3 (Hora com id)
bool oledOk = false, oledLigada = true, fsOk = false, querCalibrar = false, wifiLigado = false;
uint32_t sessao = 0, seq = 0;
uint32_t eventosImpacto = 0, autoZeroN = 0, filaPerdidas = 0, syncN = 0, janelasDesdeCheckpoint = 0;
uint32_t ultimaBatidaMs = 0, inicioParado = 0, segParadoAtual = 0, secsEstados[3] = {0, 0, 0};
uint32_t ultimoToqueOled = 0, ultimoCliente = 0;
bool lacunaPendente = false, jerkReset = true, paradoAtivo = false;

// ---- Tarefa do sensor (dona do barramento Wire do MPU) ----
QueueHandle_t filaAmostras = nullptr;
volatile uint8_t enderecoMPU = 0;
volatile uint32_t fifoLacunas = 0, i2cFalhas = 0, mpuInicios = 0, filaDescartadas = 0, filaMaxUso = 0;
volatile float chipTemp = 0;
uint32_t vistoLacunas = 0, vistoFalhas = 0, vistoInicios = 0, vistoDescartadas = 0;

// ---- Relogio (64 bits, sincronizado pelo navegador; sobrevive a reset de software via RTC) ----
bool relogioOk = false; int64_t relogioOffsetMs = 0; const char *relogioOrigem = "nenhuma";
// Relogio UNICO do firmware: ms desde o boot em 64 bits (+ deslocamento de teste). ms32() = 32 bits
// baixos (carimbo das amostras). Compare instantes de 32 bits SO por diferenca (a - b) entre instantes
// proximos; para duracoes longas use ms64(). Nada usa ms32() diretamente.
// API: ms64 — Retorna tempo monotônico em milissegundos usando o timer do ESP32-S3, com offset apenas para testes.
uint64_t ms64() { return (uint64_t)(esp_timer_get_time() / 1000LL) + TESTE_OFFSET_MS; }
// API: ms32 — Fornece os 32 bits baixos do relógio para carimbos compactos do sensor; operações devem considerar wraparound.
uint32_t ms32() { return (uint32_t)ms64(); }
// API: epochMs — Converte uptime monotônico em horário absoluto apenas quando o relógio foi sincronizado.
uint64_t epochMs() { return relogioOk ? (uint64_t)((int64_t)ms64() + relogioOffsetMs) : 0ULL; }
// Converte o carimbo de 32 bits de uma amostra (millis) em ms de 64 bits / epoch daquele instante.
// API: ms64DeAmostra — Reconstrói o tempo monotônico de uma leitura a partir do carimbo compacto produzido na aquisição.
uint64_t ms64DeAmostra(uint32_t t) { const uint64_t m = ms64(); return m - (uint32_t)((uint32_t)m - t); }
// API: epochDeAmostra — Converte o instante efetivo da amostra para data/hora; evita usar o tempo do processamento.
uint64_t epochDeAmostra(uint32_t t) { return relogioOk ? (uint64_t)((int64_t)ms64DeAmostra(t) + relogioOffsetMs) : 0ULL; }

// ---- Processamento ----
float biasG[3] = {0, 0, 0}, grav[3] = {0, 0, 0}, antA[3] = {0, 0, 0};
bool primeiroDado = true;
float somaAcc2 = 0, somaGiro2 = 0, somaJerk = 0, somaGrav[3] = {0, 0, 0}, somaGr[3] = {0, 0, 0}, somaGr2[3] = {0, 0, 0};
uint16_t nJanela = 0;
float accAtual = 0, giroAtual = 0, jerkAtual = 0, pitchAtual = 0, rollAtual = 0;
float gravRec[3][3]; uint8_t gravRecPos = 0, gravRecQtd = 0;
bool refNormal = false, refBaixa = false; float gU[3] = {0, 0, 1}, gD[3] = {0, 0, 1};
float cabecaGraus = 0; bool cabecaBaixa = false;
// passos (trem de picos)
float passoLp = 0; bool passoAcima = false; uint32_t ultimoPico = 0, passosTotal = 0, picosDescartados = 0;
uint8_t tremLen = 0, passosPendentes = 0;
uint16_t passosJanela = 0, passos5[5] = {0, 0, 0, 0, 0}; uint8_t passos5Pos = 0;
// ritmo
float sinalRitmo[3][TAM_RITMO]; uint16_t ritmoPos = 0, ritmoQtd = 0;
float ritmoHz = 0, ritmoQual = 0; int8_t ritmoEixo = -1;
// calibracao do giro
bool calibAtiva = false; uint16_t calibN = 0; float calS[3], calS2[3], calAmin, calAmax;
char calibMsg[64] = "aguardando";
uint8_t recentMotion[60] = {0}; uint8_t recentPos = 0; uint16_t recentSum = 0;
Med hist[TAM_HIST]; uint16_t histPos = 0, histQtd = 0;
uint8_t aggN = 0; float aggAcc = 0, aggGiro = 0, aggJerk = 0; uint16_t aggPassos = 0, aggCabeca = 0, aggEst[3], aggCand[N_CAND];
bool resumoCheio = false;
Hora horas[TAM_HORAS]; uint16_t horasPos = 0, horasQtd = 0; Hora horaAtual; bool horaIniciada = false;
Hora horaAnterior; bool temHoraAnterior = false;
float indiceAtividade = -1; bool alertaAtivo = false; const char *indiceMotivo = "aguardando dados";
float bateriaVolts = -1.0f, bateriaLeituraBruta = 0; uint32_t ultimaLeituraBateria = 0; bool bateriaLida = false; const char *bateriaEstado = "SEM_LEITURA";
uint8_t ledPiscadas = 0; uint32_t ledProx = 0; bool ledEstado = false; uint32_t oledSyncAte = 0;
bool btnPress = false, btnLongo = false; uint32_t btnIni = 0;

// ---- Arquivos ----
enum Arq : uint8_t { A_EVENTOS = 0, A_RESUMO, A_HORAS };
const char *ARQ_PATH[] = {"/eventos.csv", "/resumo.csv", "/horas.csv"};
const char *CAB_EVENTOS = "sessao,ms,epoch_ms,animal,tipo,detalhe";
const char *CAB_RESUMO = "sessao,ms,epoch_ms,animal,acc_rms_g,giro_rms_dps,jerk_gps,pitch_deg,roll_deg,cabeca_graus,cabeca_baixa_s,passos,ritmo_hz,ritmo_qual,estado,candidato,rotulo_humano,s_parado,s_leve,s_intenso";
const char *CAB_HORAS = "chave,relogio,epoch_ini_s,seg_parado,seg_locomocao,seg_cabeca_baixa,seg_ritmo,seg_outro,s_parado,s_leve,s_intenso,cabeca_baixa_s,passos,impactos,seg_validos,seg_id";
LinhaPend fila[16]; uint8_t filaQtd = 0; bool baixando = false; int8_t arqBaixando = -1; // so o arquivo em download e adiado
File arquivoRaw; bool rawAtivo = false; uint32_t rawInicio = 0, rawFimMs = 0, rawCont = 0;
RawReg rawBuf[100]; uint8_t rawBufN = 0;

#if HABILITAR_LORA
#include <SPI.h>
#include "SmartCollar_lora.h"
SX1262 radio = new Module(8, 14, 12, 13); // NSS, DIO1, RST, BUSY (Heltec V3)
enum LoraFase : uint8_t { L_OCIOSO, L_TX, L_RX_ACK, L_ESPERA };
volatile bool loraIrq = false; bool loraPronto = false; LoraFase loraFase = L_OCIOSO;
uint32_t loraTFase = 0, loraProxTent = 0, ultimaTx = 0, ultimaInitLora = 0;
uint32_t loraTxN = 0, loraAckN = 0, loraFalhasN = 0, loraTravN = 0; int8_t loraUltRssiBase = 0;
uint8_t loraTent = 0; uint16_t seqTx = 0;
PacoteLoRa loraPend;
void IRAM_ATTR loraIsr() { loraIrq = true; }
#endif

// ======================= UTILITARIOS =======================
// API: mudarRotulo — Registra uma mudança de rótulo com o instante do clique para evitar rotular amostras antigas.
void mudarRotulo(uint8_t r) {
  rotuloAtual = r;
  if (mudRotQtd == 32) { memmove(mudRot, mudRot + 1, 31 * sizeof(MudancaRotulo)); mudRotQtd = 31; }
  mudRot[mudRotQtd].t = ms32(); mudRot[mudRotQtd].r = r; mudRotQtd++;
}
// Amostras chegam em ordem de tempo: aplica as mudancas cujo instante ja passou para esta amostra.
// API: rotuloEm — Resolve qual rótulo do observador estava vigente quando a amostra foi adquirida.
uint8_t rotuloEm(uint32_t t) {
  while (mudRotQtd && (int32_t)(t - mudRot[0].t) >= 0) { rotuloProc = mudRot[0].r; memmove(mudRot, mudRot + 1, (mudRotQtd - 1) * sizeof(MudancaRotulo)); mudRotQtd--; }
  return rotuloProc;
}
// API: somaCk — Calcula checksum de checkpoint, detectando alterações antes da restauração.
uint32_t somaCk(const HoraCheckpoint &c) { HoraCheckpoint t = c; t.soma = 0; const uint8_t *b = (const uint8_t *)&t; uint32_t h = 2166136261UL; for (size_t i = 0; i < sizeof(t); i++) { h ^= b[i]; h *= 16777619UL; } return h; }
const char *estadoNome(uint8_t s) { return s == 0 ? "PARADO" : (s == 1 ? "LEVE" : (s == 2 ? "INTENSO" : "ERRO")); }
// API: wdtReset — Notifica ao watchdog que o loop principal continua respondendo.
void wdtReset() { esp_task_wdt_reset(); }
// API: iniciarWDT — Configura o watchdog do loop; a tarefa de aquisição requer verificação de progresso própria.
void iniciarWDT() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t c; c.timeout_ms = WDT_S * 1000; c.idle_core_mask = 0; c.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&c) != ESP_OK) esp_task_wdt_init(&c);
#else
  esp_task_wdt_init(WDT_S, true);
#endif
  esp_task_wdt_add(NULL);
}
const char *motivoReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "LIGADO"; case ESP_RST_SW: return "SOFTWARE"; case ESP_RST_PANIC: return "PANICO";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "QUEDA_ALIMENTACAO"; case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default: return "OUTRO";
  }
}
// API: norma3 — Calcula o comprimento euclidiano de um vetor 3D.
float norma3(const float *v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
// API: normalizar — Normaliza um vetor 3D não nulo in-place.
void normalizar(float *v) { float n = norma3(v); if (n > 1e-6f) { v[0] /= n; v[1] /= n; v[2] /= n; } }
// API: dot3 — Calcula o produto escalar para obter projeções e ângulos.
float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
// API: cross3 — Calcula o produto vetorial, útil na orientação da cabeça.
void cross3(const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }

// API: espacoDisponivel — Protege uma reserva de armazenamento antes de aceitar novas gravações.
bool espacoDisponivel(size_t adicionais, size_t reserva = 45000) { if (!fsOk) return false; size_t t = LittleFS.totalBytes(), u = LittleFS.usedBytes(); return t > 0 && u + adicionais + reserva < t; }
const size_t RESERVA_ARQ[] = {20000, 45000, 8000};  // eventos, resumo, horas (horas tem prioridade)
// API: garantirCabecalho — Cria ou atualiza cabeçalho de CSV e trata formatos antigos conforme a versão atual.
void garantirCabecalho(const char *path, const char *cab, const char *pathAntigo) {
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path, FILE_READ); String l = f ? f.readStringUntil('\n') : String(""); if (f) f.close();
    l.trim(); if (l == cab) return;
    LittleFS.remove(pathAntigo); LittleFS.rename(path, pathAntigo);
    Serial.printf("Formato antigo %s -> %s\n", path, pathAntigo);
  }
  File f = LittleFS.open(path, FILE_WRITE); if (f) { f.println(cab); f.close(); }
}
// API: escreverAgora — Tenta persistir texto em arquivo LittleFS e retorna o resultado da operação.
bool escreverAgora(uint8_t arq, const char *txt) {   // true = linha inteira gravada e arquivo fechado
  const size_t len = strlen(txt);
#if HABILITAR_DEBUG
  if (flashCheiaTeste) return false;
#endif
  if (!fsOk || !espacoDisponivel(len + 16, RESERVA_ARQ[arq])) return false;
  File f = LittleFS.open(ARQ_PATH[arq], FILE_APPEND); if (!f) return false;
  if (arq == A_RESUMO && f.size() > RESUMO_MAX_BYTES) { resumoCheio = true; f.close(); return false; }
  if (arq == A_HORAS && f.size() > HORAS_MAX_BYTES) {
    f.close(); LittleFS.remove("/horas_antigo.csv"); LittleFS.rename("/horas.csv", "/horas_antigo.csv");
    f = LittleFS.open("/horas.csv", FILE_WRITE); if (!f) return false; f.println(CAB_HORAS);
  }
  size_t n = f.print(txt); f.close();
  return n == len;
}
// API: anexar — Adiciona uma linha ao destino; pode enfileirar quando há download ou gravação em curso.
bool anexar(uint8_t arq, const char *txt) {   // true = gravada (ou enfileirada durante download)
  if (!fsOk) return false;
  if (baixando && arq == arqBaixando) { if (filaQtd < 16) { fila[filaQtd].arq = arq; strlcpy(fila[filaQtd].txt, txt, sizeof(fila[0].txt)); filaQtd++; return true; } filaPerdidas++; return false; }
  return escreverAgora(arq, txt);
}
// API: esvaziarFila — Descarrega linhas pendentes, observando risco de erro de escrita e perda em falhas de energia.
void esvaziarFila() { for (uint8_t i = 0; i < filaQtd; i++) escreverAgora(fila[i].arq, fila[i].txt); filaQtd = 0; }
// API: evento — Registra eventos de estado e diagnóstico com horário do dispositivo.
void evento(const char *tipo, const char *detalhe) {
  Serial.printf("EVENTO %lu %s %s\n", (unsigned long)ms32(), tipo, detalhe);
  char l[160];
  snprintf(l, sizeof(l), "%lu,%llu,%llu,%s,%s,%s\n", (unsigned long)sessao, (unsigned long long)ms64(), (unsigned long long)epochMs(), ANIMAL_ID, tipo, detalhe);
  anexar(A_EVENTOS, l);
}

// ======================= DASHBOARD =======================
const char HTML[] PROGMEM = R"SMARTHTML(
<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Collar v3.1.3</title><style>
:root{color-scheme:dark;--bg:#091521;--tile:#142638;--line:#2b485d;--txt:#e6f3f8;--muted:#9db8c8;--teal:#5eead4;--yellow:#ffdb80;--red:#fca5a5}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.45 system-ui,Segoe UI,sans-serif}main{max-width:1100px;margin:auto;padding:16px}
h1{font-size:23px;margin:0}h2{font-size:16px;margin:0 0 10px}small,.sub{color:var(--muted)}.head{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;margin-bottom:14px}
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.card{background:var(--tile);border:1px solid var(--line);border-radius:14px;padding:14px;margin:0 0 10px}
.metric{font-size:23px;font-weight:720;margin-top:6px;overflow-wrap:anywhere}.lbl{font-size:12px;color:var(--muted)}.teal{color:var(--teal)}.warn{color:var(--yellow)}.bad{color:var(--red)}
button,a.btn,select{background:#284459;border:1px solid #4a6a7e;border-radius:9px;color:var(--txt);padding:8px 11px;font:inherit;cursor:pointer}
a.btn{text-decoration:none;display:inline-block}canvas{width:100%;height:190px;display:block}
.flex{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.note{font-size:12px;color:var(--muted);margin:8px 0 0}.on{border-radius:12px;padding:6px 10px;background:#1e5050}
.leg span{display:inline-block;margin-right:10px;font-size:12px}.leg i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px}
@media(max-width:800px){.grid{grid-template-columns:repeat(2,1fr)}}
</style></head><body><main>
<div class="head"><div><h1>🐄 Smart Collar · Research v3.1.3</h1><small><b id="animal">–</b> · protótipo de pesquisa, não diagnóstico veterinário</small></div><div class="on" id="online">Conectando...</div></div>
<div class="grid">
<div class="card"><div class="lbl">Intensidade</div><div class="metric teal" id="estado">–</div></div>
<div class="card"><div class="lbl">Comportamento candidato*</div><div class="metric" id="cand">–</div></div>
<div class="card"><div class="lbl">Aceleração dinâmica RMS</div><div class="metric"><span id="acc">–</span> <small>g</small></div></div>
<div class="card"><div class="lbl">Rotação RMS</div><div class="metric"><span id="giro">–</span> <small>°/s</small></div></div>
<div class="card"><div class="lbl">Cabeça (vs. referência)</div><div class="metric"><span id="cab">–</span></div><small id="cabref">–</small></div>
<div class="card"><div class="lbl">Passos estimados*</div><div class="metric"><span id="passos">–</span></div><small id="passosT">–</small></div>
<div class="card"><div class="lbl">Ritmo (mastigação?)*</div><div class="metric" id="rhythm">–</div></div>
<div class="card"><div class="lbl">Índice de atividade*</div><div class="metric" id="indice">–</div><small id="indiceN">–</small></div>
<div class="card"><div class="lbl">Movimento / último minuto</div><div class="metric"><span id="mov">–</span><small> %</small></div></div>
<div class="card"><div class="lbl">Inclinação pitch / roll</div><div class="metric"><span id="pr">–</span></div></div>
<div class="card"><div class="lbl">Bateria (tensão)</div><div class="metric"><span id="bat">–</span></div><small id="batN">–</small></div>
<div class="card"><div class="lbl">Relógio</div><div class="metric" id="clock">–</div><small id="clockN">–</small></div>
</div>
<section class="card"><h2>Orçamento por hora (últimas 24 h)</h2><canvas id="bars"></canvas><div class="leg" id="leg"></div><p class="note">Segundos por hora em cada comportamento candidato. Sem relógio sincronizado, as horas são contadas desde que o colar ligou (U0, U1...).</p></section>
<section class="card"><h2>Intensidade do movimento (5 min)</h2><canvas id="chart"></canvas></section>
<div class="grid">
<section class="card" style="grid-column:span 2"><h2>Tempo desde a inicialização</h2><div id="times">–</div><p class="note">PARADO não significa dormindo, deitado ou ruminando.</p></section>
<section class="card" style="grid-column:span 2"><h2>Diagnóstico</h2><div id="diag">–</div><p class="note">Temperatura = interior do chip MPU6050, não do animal.</p></section>
</div>
<section class="card"><h2>Rotulagem humana (para treinar modelos)</h2>
<div class="flex"><select id="rotulo"><option value="0">SEM RÓTULO</option><option value="1">DEITADO</option><option value="2">EM PÉ PARADO</option><option value="3">CAMINHANDO</option><option value="4">PASTEJANDO</option><option value="5">RUMINANDO EM PÉ</option><option value="6">RUMINANDO DEITADO</option><option value="7">COCHO</option><option value="8">BEBENDO</option><option value="9">OUTRO</option></select>
<button id="aplicar">Marcar observação</button><button id="sync">Marcador de vídeo (LED pisca)</button></div>
<p class="note">Filme o colar ao tocar no marcador: o LED branco pisca 3× e o OLED mostra SYNC. O botão PRG da placa faz o mesmo.</p></section>
<section class="card"><h2>Calibração</h2><div class="flex"><button id="calG">Giroscópio (sensor imóvel 3 s)</button><button id="calN">Cabeça NORMAL (em pé, cabeça na altura usual)</button><button id="calB">Cabeça BAIXA (pastejando)</button><button id="calL">Limpar cabeça</button></div>
<p class="note" id="calMsg">–</p><p class="note">Use um contrapeso no colar para ele não girar no pescoço; se girar, recalibre a cabeça.</p></section>
<section class="card"><h2>Coleta e arquivos</h2><div class="flex"><select id="rawMin"><option>3</option><option selected>10</option><option>30</option><option>60</option></select><button id="iniciarRaw">Gravar bruto (min)</button><button id="pararRaw">Parar</button></div>
<div class="flex" style="margin-top:10px"><a class="btn" href="/csv/summary">Resumo 10 s</a><a class="btn" href="/csv/hours">Horas</a><a class="btn" href="/csv/events">Eventos/rótulos</a><a class="btn" href="/csv/raw">Bruto (CSV)</a><a class="btn" href="/bin/raw">Bruto (.bin)</a><button id="novaSessao">Nova sessão (apaga logs)</button></div>
<p class="note" id="files">–</p><p class="note">Downloads ficam bloqueados enquanto o bruto grava. O relógio é ajustado automaticamente quando este painel abre.</p></section>
<section class="card"><h2>Limites</h2><p class="note">* Heurísticas NÃO validadas. Sem GPS, temperatura corporal ou diagnóstico de ruminação/cio/doença. Wi-Fi local sem criptografia HTTP. Não instalar em animais sem caixa e fixação seguras.</p></section>
</main><script>
const $=x=>document.getElementById(x);let pts=[],seq=-1,down=0;
const fmt=(x,d=2)=>Number.isFinite(x)?Number(x).toFixed(d):'–';
const CN=['Parado','Locomoção','Cabeça baixa','Ritmo mastig.','Outro ativo'],CC=['#64748b','#f59e0b','#22c55e','#a78bfa','#38bdf8'];
$('leg').innerHTML=CN.map((n,i)=>'<span><i style="background:'+CC[i]+'"></i>'+n+'</span>').join('');
function prep(c,h){const b=c.getBoundingClientRect(),z=Math.max(1,devicePixelRatio||1);c.width=Math.round(b.width*z);c.height=Math.round(h*z);const g=c.getContext('2d');g.setTransform(z,0,0,z,0,0);g.clearRect(0,0,b.width,h);return[g,b.width]}
function graph(){const[g,w]=prep($('chart'),190),p=35;g.strokeStyle='#315168';for(let k=0;k<5;k++){let y=12+k*40;g.beginPath();g.moveTo(p,y);g.lineTo(w-8,y);g.stroke()}
let ym=Math.max(.15,...pts.map(x=>x.a))*1.1;g.fillStyle='#b2c8d6';g.font='11px system-ui';g.fillText(fmt(ym,2)+'g',2,12);g.fillText('0',17,175);if(pts.length<2)return;
g.lineWidth=2;for(let i=1;i<pts.length;i++){let x0=p+(w-p-8)*(i-1)/(pts.length-1),x1=p+(w-p-8)*i/(pts.length-1);g.strokeStyle=CC[pts[i].c]||'#5eead4';g.beginPath();g.moveTo(x0,12+160*(1-pts[i-1].a/ym));g.lineTo(x1,12+160*(1-pts[i].a/ym));g.stroke()}}
let H=[];function bars(){const[g,w]=prep($('bars'),190),p=30,n=24,bw=(w-p-6)/n;g.fillStyle='#b2c8d6';g.font='10px system-ui';g.fillText('60m',2,12);g.fillText('0',14,170);
H.slice(-n).forEach((h,i)=>{let x=p+i*bw,y=166;h.s.forEach((s,k)=>{let hh=150*s/3600;g.fillStyle=CC[k];g.fillRect(x+1,y-hh,bw-2,hh);y-=hh});
let lb=h.r?String(new Date(h.e*1000).getHours()).padStart(2,'0'):'U'+(h.k&0x7fffffff);if(i%2==0||n<13){g.fillStyle='#b2c8d6';g.fillText(lb,x,184)}})}
async function req(path,body){let r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body||{})});if(!r.ok)throw Error(await r.text());return r.text()}
async function action(path,body){try{alert(await req(path,body))}catch(e){alert('Falha: '+e.message)}}
$('aplicar').onclick=()=>action('/api/label',{code:$('rotulo').value});$('sync').onclick=()=>action('/api/sync',{});
$('iniciarRaw').onclick=()=>{if(confirm('Iniciar gravação bruta? A anterior será substituída.'))action('/api/raw/start',{min:$('rawMin').value})};
$('pararRaw').onclick=()=>action('/api/raw/stop',{});
$('novaSessao').onclick=()=>{if(confirm('ATENÇÃO: baixe os CSV antes. Apagar todos os arquivos?'))action('/api/session/new',{})};
$('calG').onclick=()=>{if(confirm('Deixe o sensor IMÓVEL por 3 s.'))action('/api/calibrate',{})};
$('calN').onclick=()=>action('/api/orient',{tipo:'normal'});$('calB').onclick=()=>action('/api/orient',{tipo:'baixa'});$('calL').onclick=()=>action('/api/orient',{tipo:'limpar'});
async function update(){try{let r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error();const s=await r.json();down=0;$('online').textContent='Online · '+s.uptime_s+'s';
$('animal').textContent=s.animal;$('estado').textContent=s.estado;$('cand').textContent=CN[s.cand_i]||s.candidato;$('cand').style.color=CC[s.cand_i];
$('acc').textContent=fmt(s.acc,3);$('giro').textContent=fmt(s.giro,1);$('mov').textContent=fmt(s.movimento_min,0);$('pr').textContent=fmt(s.pitch,0)+'° / '+fmt(s.roll,0)+'°';
$('cab').textContent=fmt(s.cabeca,0)+'°'+(s.cabeca_baixa?' · BAIXA':'');$('cabref').textContent=['sem referência (use Calibração)','ref. normal','ref. normal + baixa'][s.cabeca_ref];
$('passos').textContent=s.passos_hora+' /h';$('passosT').textContent='total '+s.passos_total+' · 5 s: '+s.passos_5s+' · picos descartados '+s.picos_descartados;
$('rhythm').textContent=s.ritmo_ok?fmt(s.ritmo_hz,2)+' Hz · q'+fmt(s.ritmo_qual,2):'–';
$('indice').textContent=s.indice===null?'–':fmt(s.indice,2)+'×';$('indice').className='metric'+(s.alerta?' bad':'');$('indiceN').textContent=s.alerta?'ALTO: verifique o animal (possível cio?)':s.indice_motivo;
$('bat').textContent=s.bateria_v===null?'Sem leitura':fmt(s.bateria_v,2)+' V';$('batN').textContent=s.bateria_estado+' · bruta '+fmt(s.bateria_bruta_v,2)+' V · sem % real';$('bat').className=s.bateria_estado=='CRITICA'?'bad':(s.bateria_estado=='BAIXA'?'warn':'');
$('clock').textContent=s.relogio?new Date(s.epoch_ms).toLocaleTimeString():'não sinc.';$('clockN').textContent=s.relogio?'origem: '+s.relogio_origem:'abra este painel para ajustar';
$('times').textContent='Parado '+s.parado_s+'s · Leve '+s.leve_s+'s · Intenso '+s.intenso_s+'s · Imóvel agora '+s.parado_atual_s+'s';
$('diag').textContent='MPU '+(s.sensor?'OK':'ERRO')+' '+s.i2c+' · chip '+fmt(s.temp_chip_c,1)+'°C · impactos '+s.eventos+' · lacunas FIFO '+s.fifo_lacunas+' · falhas I2C '+s.i2c_falhas+' · fila máx '+s.fila_max+'/'+s.fila_cap+' (descartadas '+s.fila_descartadas+')'+' · auto-zero '+s.autozero+' · checkpoint falhas '+s.ck_falhas+(s.horas_pendentes?' · HORAS PENDENTES (NVS): '+s.horas_pendentes:'')+(s.horas_perdidas?' · HORAS PERDIDAS: '+s.horas_perdidas:'')+' · boot '+s.boot_id.toString(16)+' · heap '+s.heap+' · Wi-Fi clientes '+s.wifi_clientes+(s.lora>0?' · LoRa TX '+s.lora_tx+' ACK '+s.lora_ack+' falhas '+s.lora_falhas+' reinícios '+s.lora_trav:(s.lora==-2?' · LoRa ERRO':''));
$('calMsg').textContent='Giroscópio: '+s.calib;
$('files').textContent='LittleFS '+(s.fs?'OK ('+Math.round(s.fs_livre/1024)+' kB livres)':'INATIVO')+(s.resumo_cheio?' · RESUMO CHEIO':'')+' · Bruto: '+(s.raw?'GRAVANDO '+s.raw_s+'s / '+s.raw_lim_s+'s':'parado')+' · '+s.raw_count+' amostras · Rótulo observador: '+s.rotulo+' / nos dados: '+s.rotulo_dados+(s.teste?' · MODO TESTE: '+s.teste:'')+' · Sessão '+s.sessao;
if(s.seq!==seq){seq=s.seq;pts.push({a:s.acc,c:s.cand_i});if(pts.length>300)pts.shift();graph()}
}catch(e){if(++down>1)$('online').textContent='Sem conexão'}}
async function carregarHist(){try{let v=await(await fetch('/api/history',{cache:'no-store'})).json();pts=v.map(o=>({a:o.a,c:o.c}));graph()}catch(e){}}
async function carregarHoras(){try{H=await(await fetch('/api/hours?n=24',{cache:'no-store'})).json();bars()}catch(e){}}
window.addEventListener('resize',()=>{graph();bars()});
req('/api/time',{ms:Date.now()}).catch(()=>{}).then(carregarHist).then(carregarHoras).then(update);setInterval(update,1000);setInterval(carregarHoras,60000);
</script></body></html>
)SMARTHTML";

// ======================= MPU6050 (usado SOMENTE pela tarefa do sensor) =======================
// API: responde — Detecta a presença do endereço I2C do sensor.
bool responde(uint8_t addr) { Wire.beginTransmission(addr); return Wire.endTransmission() == 0; }
// API: regSet — Escreve um registrador MPU6050 via I2C.
bool regSet(uint8_t r, uint8_t v) { Wire.beginTransmission(enderecoMPU); Wire.write(r); Wire.write(v); return Wire.endTransmission() == 0; }
// API: lerBytes — Lê bloco de registradores do MPU6050 e valida a transação I2C.
bool lerBytes(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(enderecoMPU); Wire.write(reg); if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)enderecoMPU, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
// API: regRead — Lê um registrador MPU6050 de um byte.
bool regRead(uint8_t r, uint8_t &v) { return lerBytes(r, &v, 1); }
// API: resetFIFO — Reinicializa a FIFO do MPU6050 antes de coletar novas amostras.
bool resetFIFO() { return regSet(0x6A, 0x04) && regSet(0x6A, 0x40); }
// API: iniciarMPU — Inicializa e configura o sensor com escalas e taxa de amostragem do experimento.
bool iniciarMPU() {
  uint8_t a; if (responde(0x68)) a = 0x68; else if (responde(0x69)) a = 0x69; else return false;
  enderecoMPU = a;
  uint8_t who = 0; if (!regRead(0x75, who)) { enderecoMPU = 0; return false; }
  bool ok = regSet(0x6B, 0x80); vTaskDelay(pdMS_TO_TICKS(100)); // reset
  ok = ok && regSet(0x6B, 0x01); vTaskDelay(pdMS_TO_TICKS(10)); // PLL do giro X
  ok = ok && regSet(0x1A, 0x05) && regSet(0x19, SMPLRT_DIV) && regSet(0x1B, 0x08) && regSet(0x1C, 0x08)
          && regSet(0x38, 0x10) && regSet(0x23, 0x78) && resetFIFO();
  if (!ok) enderecoMPU = 0;
  return ok;
}
// API: tarefaSensor — Tarefa FreeRTOS de aquisição: esvazia FIFO física e entrega leituras à fila em RAM.
void tarefaSensor(void *) {
  double tUlt = 0; bool tOk = false, lacuna = true; uint32_t ultimaTent = 0, ultimaTemp = 0; uint8_t buf[120];
  for (;;) {
    if (!enderecoMPU) {
      if (ms32() - ultimaTent >= 1400) { ultimaTent = ms32(); if (iniciarMPU()) { mpuInicios = mpuInicios + 1; tOk = false; lacuna = true; } }
      vTaskDelay(pdMS_TO_TICKS(50)); continue;
    }
    uint8_t st, c[2];
    if (!regRead(0x3A, st) || !lerBytes(0x72, c, 2)) { i2cFalhas = i2cFalhas + 1; enderecoMPU = 0; continue; }
    uint16_t n = ((uint16_t)c[0] << 8) | c[1];
    if ((st & 0x10) || n >= 1020 || (n % 12)) { fifoLacunas = fifoLacunas + 1; resetFIFO(); lacuna = true; tOk = false; vTaskDelay(pdMS_TO_TICKS(PERIODO_MS)); continue; }
    const uint16_t total = n / 12;
    if (total) {
      // Carimbo de tempo: a ultima amostra lida tem em media meio periodo de idade. Em regime, avanca
      // total*periodo e corrige 5% do erro (acompanha a deriva do oscilador do MPU sem saltos).
      const double agora = (double)ms32() - PERIODO_MS * 0.5;
      double ultimo;
      if (!tOk) { ultimo = agora; tOk = true; }
      else { ultimo = tUlt + total * (double)PERIODO_MS; double erro = agora - ultimo; ultimo += (fabs(erro) > 250) ? erro : 0.05 * erro; }
      tUlt = ultimo;
      uint16_t feitos = 0; bool falhou = false;
      while (feitos < total) {
        uint8_t lote = (uint8_t)min<uint16_t>(10, total - feitos);
        if (!lerBytes(0x74, buf, lote * 12)) { falhou = true; break; }
        for (uint8_t k = 0; k < lote; k++) {
          const uint8_t *p = buf + k * 12; Amostra a;
          for (int i = 0; i < 6; i++) a.r[i] = (int16_t)((p[2 * i] << 8) | p[2 * i + 1]);
          a.t = (uint32_t)llround(ultimo - (double)(total - 1 - (feitos + k)) * PERIODO_MS);
          a.flags = lacuna ? 1 : 0;
          if (xQueueSend(filaAmostras, &a, 0) == pdTRUE) lacuna = false; else { filaDescartadas = filaDescartadas + 1; lacuna = true; }
        }
        feitos += lote;
      }
      if (falhou) { i2cFalhas = i2cFalhas + 1; enderecoMPU = 0; continue; }
      uint32_t uso = FILA_AMOSTRAS - uxQueueSpacesAvailable(filaAmostras); if (uso > filaMaxUso) filaMaxUso = uso;
    }
    if (ms32() - ultimaTemp >= 1000) { ultimaTemp = ms32(); uint8_t t[2]; if (lerBytes(0x41, t, 2)) chipTemp = (int16_t)((t[0] << 8) | t[1]) / 340.0f + 36.53f; }
    vTaskDelay(pdMS_TO_TICKS(100)); // FIFO guarda ~4 s; 100 ms deixa muita margem
  }
}

// ======================= CALIBRACAO =======================
// API: iniciarCalibGiro — Inicia coleta de giroscópio para calibração com dispositivo parado.
void iniciarCalibGiro() { calibAtiva = true; calibN = 0; for (int i = 0; i < 3; i++) { calS[i] = 0; calS2[i] = 0; } calAmin = 99; calAmax = 0; strlcpy(calibMsg, "em andamento (3 s)...", sizeof(calibMsg)); }
// API: salvarBias — Persiste os offsets de calibração do giroscópio na NVS.
void salvarBias() { prefs.putFloat("bgx", biasG[0]); prefs.putFloat("bgy", biasG[1]); prefs.putFloat("bgz", biasG[2]); prefs.putBool("bok", true); }
// API: finalizarCalibGiro — Verifica estabilidade das amostras e aceita ou rejeita a calibração.
void finalizarCalibGiro() {
  calibAtiva = false; float med[3], dp[3]; float pior = 0;
  for (int i = 0; i < 3; i++) { med[i] = calS[i] / calibN; dp[i] = sqrtf(fmaxf(0, calS2[i] / calibN - med[i] * med[i])); pior = fmaxf(pior, dp[i]); }
  if (pior < 1.0f && (calAmax - calAmin) < 0.05f) {
    for (int i = 0; i < 3; i++) biasG[i] = med[i];
    salvarBias(); snprintf(calibMsg, sizeof(calibMsg), "OK bias %.2f/%.2f/%.2f d/s", med[0], med[1], med[2]); evento("CALIBRACAO", "GYRO_BIAS_OK");
  } else { snprintf(calibMsg, sizeof(calibMsg), "RECUSADA: movimento (dp %.1f d/s); bias mantido", pior); evento("CALIBRACAO", "RECUSADA_MOVIMENTO"); }
}

// ======================= OLED / LED =======================
// API: desenharOLED — Atualiza informações reduzidas no OLED, evitando interpretá-las como diagnóstico animal.
void desenharOLED() {
  if (!oledOk || !oledLigada) return;
  tela.clearBuffer(); tela.setFont(u8g2_font_6x10_tf);
  char l[32]; snprintf(l, sizeof(l), "SC3.1 %.9s %s%s", ANIMAL_ID, wifiLigado ? "W" : "-", rawAtivo ? " R" : "");
  tela.drawStr(0, 9, l); tela.drawHLine(0, 12, 128);
  tela.setFont(u8g2_font_helvB12_tr);
  if ((int32_t)(oledSyncAte - ms32()) > 0) { snprintf(l, sizeof(l), "SYNC #%lu", (unsigned long)syncN); tela.drawStr(0, 32, l); }
  else if (!enderecoMPU) tela.drawStr(0, 32, "ERRO MPU");
  else tela.drawStr(0, 32, estadoAtual > 2 ? "INICIANDO" : CAND_NOME[candAtual]);
  tela.setFont(u8g2_font_6x10_tf);
  snprintf(l, sizeof(l), "ACC %.3fg GIR %.0f", accAtual, giroAtual); tela.drawStr(0, 45, l);
  snprintf(l, sizeof(l), "CAB %.0f%s P/h %lu", cabecaGraus, cabecaBaixa ? "B" : "", (unsigned long)(horaIniciada ? horaAtual.passos : 0)); tela.drawStr(0, 55, l);
  if (bateriaVolts > 0) snprintf(l, sizeof(l), "BAT %.2fV %s", bateriaVolts, relogioOk ? "RELOGIO" : "SEM RELOGIO");
  else snprintf(l, sizeof(l), "BAT ---  %s", relogioOk ? "RELOGIO" : "SEM RELOGIO");
  tela.drawStr(0, 64, l); tela.sendBuffer();
}
// API: acordarOLED — Reativa o OLED após interação e atualiza prazo de suspensão.
void acordarOLED() { ultimoToqueOled = ms32(); if (oledOk && !oledLigada) { oledLigada = true; tela.setPowerSave(0); } }
// API: iniciarOLED — Configura a alimentação e o controlador OLED da Heltec.
void iniciarOLED() {
  pinMode(PIN_OLED_PWR, OUTPUT); digitalWrite(PIN_OLED_PWR, LOW); delay(300); pinMode(PIN_OLED_RST, OUTPUT);
  digitalWrite(PIN_OLED_RST, LOW); delay(30); digitalWrite(PIN_OLED_RST, HIGH); delay(120);
  scannerOLED.begin(PIN_OLED_SDA, PIN_OLED_SCL, 100000); scannerOLED.beginTransmission(0x3C); int a = scannerOLED.endTransmission();
  scannerOLED.beginTransmission(0x3D); int b = scannerOLED.endTransmission(); scannerOLED.end();
  if (a != 0 && b == 0) tela.setI2CAddress(0x3D * 2);
  tela.begin(); tela.setContrast(255); oledOk = (a == 0 || b == 0); ultimoToqueOled = ms32(); desenharOLED();
}
// API: servicoLedOled — Atualiza LED e desligamento automático do display no loop principal.
void servicoLedOled() {
  uint32_t agora = ms32();
  if (ledPiscadas && (int32_t)(agora - ledProx) >= 0) { ledEstado = !ledEstado; digitalWrite(PIN_LED, ledEstado); ledProx = agora + 180; if (!ledEstado) ledPiscadas--; }
  if (OLED_AUTO_DESLIGA_S && oledLigada && oledOk && agora - ultimoToqueOled > OLED_AUTO_DESLIGA_S * 1000UL) { oledLigada = false; tela.setPowerSave(1); }
}
// API: marcarSync — Dispara marcador de sincronização para alinhar gravação com vídeo externo.
void marcarSync(const char *origem) {
  syncN++; ledPiscadas = 3; ledEstado = false; ledProx = ms32(); oledSyncAte = ms32() + 3000; acordarOLED();
  char d[40]; snprintf(d, sizeof(d), "VIDEO_MARCA_%lu_%s", (unsigned long)syncN, origem); evento("SINCRONIZACAO", d); desenharOLED();
}

// ======================= HORAS / CHECKPOINT / INDICE =======================
// API: chaveHoraAgora — Obtém o identificador do intervalo horário corrente (ou relativo sem relógio).
uint32_t chaveHoraAgora() { return relogioOk ? (uint32_t)(epochMs() / DURACAO_HORA_MS) : ((uint32_t)(ms64() / DURACAO_HORA_MS) | 0x80000000UL); }
// API: chaveHoraDe — Obtém a chave horária referente ao instante de aquisição de uma amostra.
uint32_t chaveHoraDe(uint32_t t) { return relogioOk ? (uint32_t)(epochDeAmostra(t) / DURACAO_HORA_MS) : ((uint32_t)(ms64DeAmostra(t) / DURACAO_HORA_MS) | 0x80000000UL); }
// API: iniciarHora — Inicializa um novo segmento horário com identificador único.
void iniciarHora(uint32_t t) { memset(&horaAtual, 0, sizeof(horaAtual)); horaAtual.id = esp_random() | 1; horaAtual.chave = chaveHoraDe(t); horaAtual.epochIni = relogioOk ? (uint32_t)(epochDeAmostra(t) / 1000ULL) : 0; horaIniciada = true; }
// API: empurrarHora — Adiciona segmento encerrado ao histórico circular disponível no painel.
void empurrarHora(const Hora &h) { horas[horasPos] = h; horasPos = (horasPos + 1) % TAM_HORAS; if (horasQtd < TAM_HORAS) horasQtd++; }
const Hora &horaIdx(int i) { return horas[(horasPos + TAM_HORAS - horasQtd + i) % TAM_HORAS]; }
// API: lerSlot — Lê e valida um dos dois slots de checkpoint antes de restaurar.
bool lerSlot(uint8_t i, HoraCheckpoint &c) {
  if (!fsOk || !LittleFS.exists(CK_SLOT[i])) return false;
  File f = LittleFS.open(CK_SLOT[i], FILE_READ); bool ok = f && f.read((uint8_t *)&c, sizeof(c)) == sizeof(c); if (f) f.close();
  return ok && c.magic == CK_MAGIC && c.soma == somaCk(c) && c.sessao == sessao;
}
// Dois slots alternados: a gravacao nova nunca sobrescreve a unica copia valida. Le de volta para confirmar.
// API: salvarCheckpointHora — Grava checkpoint alternado com sequência e checksum; verificar durabilidade nos testes.
void salvarCheckpointHora() {
  janelasDesdeCheckpoint = 0;
  if (!fsOk || !horaIniciada) return;  // arquivo proprio: pode gravar mesmo durante downloads
  HoraCheckpoint c; memset(&c, 0, sizeof(c)); c.magic = CK_MAGIC; c.sessao = sessao; c.seq = ++ckSeq; c.h = horaAtual; c.soma = somaCk(c);
  const char *slot = CK_SLOT[c.seq & 1]; bool ok = false;
  File f = LittleFS.open(slot, FILE_WRITE); if (f) { ok = f.write((const uint8_t *)&c, sizeof(c)) == sizeof(c); f.close(); }
  HoraCheckpoint v; ok = ok && lerSlot(c.seq & 1, v) && v.seq == c.seq;
  if (!ok && (ckFalhas++ % 30) == 0) evento("CHECKPOINT", "FALHA_GRAVACAO");
}
// API: apagarCheckpoints — Remove os checkpoints somente após os dados estarem confirmados em armazenamento durável.
void apagarCheckpoints() { if (!fsOk) return; LittleFS.remove(CK_SLOT[0]); LittleFS.remove(CK_SLOT[1]); LittleFS.remove("/hora_atual.bin"); }
// API: checkpointRTC — Armazena cópia temporária em RAM RTC, que não sobrevive à perda total de alimentação.
void checkpointRTC() {  // RAM do RTC: barato, a cada segundo; sobrevive a reset de software/watchdog (nao a falta de energia)
  if (!horaIniciada) { rtcCk.magic = 0; return; }
  rtcCk.magic = CK_MAGIC; rtcCk.sessao = sessao; rtcCk.seq = ckSeq; rtcCk.h = horaAtual; rtcCk.soma = somaCk(rtcCk);
}
// Soma registros da mesma chave (uma hora pode ter sido dividida por reinicio) e devolve passos/hora
// API: taxaPassosHora — Estima a taxa de passos associada à chave horária informada.
bool taxaPassosHora(uint32_t chave, float &taxa) {
  uint32_t p = 0, v = 0;
  for (int i = 0; i < horasQtd; i++) { const Hora &h = horaIdx(i); if (h.chave == chave) { p += h.passos; v += h.validos; } }
  if (v < 3000) return false;
  taxa = p * (DURACAO_HORA_MS / 1000.0f) / v; return true;
}
// API: calcularIndice — Compara atividade recente com a mesma hora em dias anteriores; indicador ainda não validado de cio.
void calcularIndice(uint32_t kRef) {   // kRef = hora corrente; compara kRef-1 e kRef-2
  indiceAtividade = -1;
  if (!relogioOk) { indiceMotivo = "precisa de relogio (abra o painel)"; return; }
  // compara as 2 ultimas horas completas com as MESMAS horas do dia nos 7 dias anteriores
  const uint32_t k = kRef; float rec = 0, base = 0; int nh = 0;
  for (uint32_t a = 1; a <= 2; a++) {
    float t; if (!taxaPassosHora(k - a, t)) continue;
    float soma = 0; int nd = 0;
    for (uint32_t d = 1; d <= 7; d++) { float tb; if (taxaPassosHora(k - a - 24 * d, tb)) { soma += tb; nd++; } }
    if (nd < MIN_DIAS_BASE) continue;
    rec += t; base += soma / nd; nh++;
  }
  if (nh == 0) { indiceMotivo = "precisa de 3+ dias com relogio nas mesmas horas"; return; }
  if (base < 1.0f) { indiceMotivo = "base sem passos"; return; }
  indiceAtividade = rec / base; indiceMotivo = "mesma hora do dia, 7 dias";
  bool alerta = indiceAtividade >= INDICE_ALERTA;
  if (alerta && !alertaAtivo) evento("ALERTA_ATIVIDADE", "INDICE_ALTO_VERIFICAR_ANIMAL");
  alertaAtivo = alerta;
}
// API: jaGravado — Verifica se um segmento horário já consta da lista de IDs persistidos.
bool jaGravado(uint32_t id) { for (int i = 0; i < idsQtd; i++) if (idsGravados[i] == id) return true; return false; }
// API: marcarGravado — Registra ID gravado para reduzir duplicações após reinícios.
void marcarGravado(uint32_t id) { if (!id || jaGravado(id)) return; idsGravados[idsPos] = id; idsPos = (idsPos + 1) % MAX_IDS; if (idsQtd < MAX_IDS) idsQtd++; }
// API: emPendente — Procura um segmento na fila durável de horas não confirmadas.
bool emPendente(uint32_t id) { for (int i = 0; i < pendQtd; i++) if (pend[i].id == id) return true; return false; }
// horas.csv: escrita DIRETA (nunca pela fila de download). true = linha inteira confirmada na flash.
// API: gravarLinhaHora — Tenta anexar linha horária ao CSV e deduplicar pelo identificador do segmento.
bool gravarLinhaHora(const Hora &h) {
  if (h.id && jaGravado(h.id)) return true;                    // idempotente: ja esta no arquivo
  if (baixando && arqBaixando == A_HORAS) return false;        // arquivo sendo baixado: vai para a fila duravel
  char l[200];
  snprintf(l, sizeof(l), "%lu,%u,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%u,%u,%08lX\n", (unsigned long)h.chave, (h.chave & 0x80000000UL) ? 0u : 1u, (unsigned long)h.epochIni,
           h.seg[0], h.seg[1], h.seg[2], h.seg[3], h.seg[4], h.segEst[0], h.segEst[1], h.segEst[2], h.cabecaBaixaS, (unsigned long)h.passos, h.impactos, h.validos, (unsigned long)h.id);
  if (!escreverAgora(A_HORAS, l)) return false;
  marcarGravado(h.id); return true;
}
// API: salvarPendNVS — Grava fila de horas pendentes na partição NVS, independente do LittleFS.
bool salvarPendNVS() {   // NVS: particao propria, independente da flash de arquivos
  if (pendQtd == 0) { prefs.remove("pend"); return true; }
  return prefs.putBytes("pend", pend, pendQtd * sizeof(Hora)) == pendQtd * sizeof(Hora);
}
// API: carregarPendNVS — Recupera a fila durável na inicialização, após falha de armazenamento anterior.
void carregarPendNVS() {
  size_t n = prefs.getBytesLength("pend"); if (n == 0 || n % sizeof(Hora) || n > sizeof(pend)) { pendQtd = 0; return; }
  pendQtd = prefs.getBytes("pend", pend, n) / sizeof(Hora);
}
// true = a hora esta DURAVEL (no CSV ou na fila NVS)
// API: guardarPendente — Coloca uma hora não salva na fila NVS quando o CSV não aceita escrita.
bool guardarPendente(const Hora &h) {
  if (emPendente(h.id)) return true;
  horasNaoGravadas++;
  if (pendQtd == MAX_PEND) { memmove(pend, pend + 1, (MAX_PEND - 1) * sizeof(Hora)); pendQtd--; horasPerdidas++; evento("HORA_NAO_GRAVADA", "FILA_DURAVEL_CHEIA_MAIS_ANTIGA_DESCARTADA"); }
  pend[pendQtd++] = h; ultimaTentPendente = ms32();
  bool ok = salvarPendNVS();
  evento("HORA_NAO_GRAVADA", ok ? "NA_FILA_DURAVEL_NVS" : "FALHA_NVS_SO_EM_RAM");
  return ok;
}
// API: servicoPendente — Tenta novamente persistir horas pendentes e as elimina após confirmação.
void servicoPendente() {
  if (!pendQtd || ms32() - ultimaTentPendente < 60000) return;
  ultimaTentPendente = ms32(); bool mudou = false;
  while (pendQtd && gravarLinhaHora(pend[0])) { memmove(pend, pend + 1, (pendQtd - 1) * sizeof(Hora)); pendQtd--; mudou = true; evento("HORA", "PENDENTE_GRAVADA_CONFIRMADA"); }
  if (mudou) salvarPendNVS();
}
// Persistir uma hora fechada: CSV confirmado OU fila duravel. So depois disso o checkpoint pode mudar.
// API: persistirHora — Escolhe gravação direta ou fila NVS, retornando verdadeiro apenas quando uma via funciona.
bool persistirHora(const Hora &h) { return gravarLinhaHora(h) || guardarPendente(h); }
// API: fecharHora — Conclui contadores do período e tenta persistir; atente ao cenário de falha dupla FS e NVS.
void fecharHora() {
  if (!horaIniciada) return;
  empurrarHora(horaAtual); persistirHora(horaAtual);
  horaAnterior = horaAtual; temHoraAnterior = true;
  calcularIndice(horaAtual.chave + 1);
}
// Ordem: (1) hora fechada duravel, (2) checkpoint novo (seq maior) e RTC. Um corte entre (1) e (2)
// deixa o checkpoint antigo, mas o seg_id dele ja esta no CSV/NVS -> a recuperacao o ignora.
// API: trocarHora — Encerra o segmento anterior e inicia o próximo usando horário da amostra.
void trocarHora(uint32_t t) { fecharHora(); iniciarHora(t); salvarCheckpointHora(); checkpointRTC(); }
// API: lerLinhaHora — Analisa uma linha CSV horária com validação de campos.
bool lerLinhaHora(const String &s, Hora &h) {
  unsigned long v[15]; char idHex[12] = "0";
  int n = sscanf(s.c_str(), "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%11s", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], idHex);
  if (n < 15) return false;
  memset(&h, 0, sizeof(h)); h.chave = v[0]; h.epochIni = v[2]; for (int i = 0; i < 5; i++) h.seg[i] = v[3 + i]; for (int i = 0; i < 3; i++) h.segEst[i] = v[8 + i];
  h.cabecaBaixaS = v[11]; h.passos = v[12]; h.impactos = v[13]; h.validos = v[14]; h.id = (n == 16) ? strtoul(idHex, nullptr, 16) : 0;
  return true;
}
// API: carregarArquivoHoras — Reconstrói histórico horário de arquivos já existentes.
void carregarArquivoHoras(const char *path) {
  if (!fsOk || !LittleFS.exists(path)) return;
  File f = LittleFS.open(path, FILE_READ); if (!f) return;
  f.readStringUntil('\n');
  while (f.available()) { String s = f.readStringUntil('\n'); Hora h; if (lerLinhaHora(s, h)) { empurrarHora(h); marcarGravado(h.id); } wdtReset(); }
  f.close();
}
// API: carregarHoras — Restaura dados consolidados do armazenamento para a RAM.
void carregarHoras() {
  carregarArquivoHoras("/horas_v312.csv"); carregarArquivoHoras("/horas_antigo.csv"); carregarArquivoHoras("/horas.csv");
  Serial.printf("Horas carregadas: %u\n", horasQtd);
}
// API: restaurarCheckpointHora — Recupera o melhor checkpoint e evita reaplicar segmentos já registrados.
void restaurarCheckpointHora() {
  // Escolhe a copia mais nova: slot com maior seq; a RAM do RTC (1 s) vence se for valida e nao mais velha.
  HoraCheckpoint a, b, c; bool okA = lerSlot(0, a), okB = lerSlot(1, b), ok = false; const char *origem = "ARQUIVO";
  if (okA && (!okB || a.seq > b.seq)) { c = a; ok = true; } else if (okB) { c = b; ok = true; }
  if (ok) ckSeq = c.seq;
  bool rtcOk = rtcCk.magic == CK_MAGIC && rtcCk.soma == somaCk(rtcCk) && rtcCk.sessao == sessao && (!ok || rtcCk.seq >= c.seq);
  if (rtcOk) { c = rtcCk; ok = true; origem = "RTC"; if (rtcCk.seq > ckSeq) ckSeq = rtcCk.seq; }
  rtcCk.magic = 0;  // invalida a copia do RTC
  if (fsOk && LittleFS.exists("/hora_atual.bin")) LittleFS.remove("/hora_atual.bin"); // formato v3.1.1
  if (!ok || c.h.validos == 0) return;   // nada a recuperar (hora recem-iniciada)
  char d[56];
  if (jaGravado(c.h.id) || emPendente(c.h.id)) {   // corte depois de gravar a hora, antes do checkpoint novo
    apagarCheckpoints(); snprintf(d, sizeof(d), "CHECKPOINT_JA_GRAVADO_IGNORADO_%08lX", (unsigned long)c.h.id); evento("HORA", d); return;
  }
  if (relogioOk && c.h.chave == chaveHoraAgora()) {
    horaAtual = c.h; horaIniciada = true;
    salvarCheckpointHora(); checkpointRTC();   // regrava JA (slot novo) antes de qualquer outra coisa
    snprintf(d, sizeof(d), "RETOMADA_%s_%us", origem, c.h.validos); evento("HORA", d);
  } else {
    empurrarHora(c.h);
    if (persistirHora(c.h)) { apagarCheckpoints(); snprintf(d, sizeof(d), "PARCIAL_RECUPERADA_%s_%us", origem, c.h.validos); evento("HORA", d); }
    // se nem CSV nem NVS aceitaram, os slots ficam: proximo boot tenta de novo (sem duplicar: seg_id)
  }
}

// ======================= ANALISE =======================
// API: calcularCabeca — Estima orientação da cabeça a partir de gravidade e referências calibradas.
void calcularCabeca(const float *gm) {
  float c[3] = {gm[0], gm[1], gm[2]}; normalizar(c);
  if (!refNormal) { cabecaGraus = 0; cabecaBaixa = false; return; }
  if (refBaixa) {
    float eixo[3]; cross3(gU, gD, eixo); float ne = norma3(eixo);
    if (ne > 0.05f) {
      for (int i = 0; i < 3; i++) eixo[i] /= ne;
      float p = dot3(c, eixo); float cp[3] = {c[0] - eixo[0] * p, c[1] - eixo[1] * p, c[2] - eixo[2] * p}; normalizar(cp);
      float x[3]; cross3(gU, cp, x);
      cabecaGraus = atan2f(dot3(x, eixo), dot3(gU, cp)) * 180.0f / PI;
      float angUD = acosf(fminf(1.0f, fmaxf(-1.0f, dot3(gU, gD)))) * 180.0f / PI;
      cabecaBaixa = cabecaGraus > CABECA_FRACAO * angUD;
      return;
    }
  }
  cabecaGraus = acosf(fminf(1.0f, fmaxf(-1.0f, dot3(gU, c)))) * 180.0f / PI;
  cabecaBaixa = cabecaGraus > CABECA_SEM_REF_GRAUS;
}
// API: ritmoValido — Verifica critérios heurísticos para marcar movimento periódico como candidato.
bool ritmoValido() { return ritmoEixo >= 0 && ritmoQual >= RITMO_QUAL_MIN && ritmoHz >= RITMO_HZ_MIN && ritmoHz <= RITMO_HZ_MAX; }
// API: calcularRitmo — Avalia periodicidade do movimento para gerar hipótese de ruminação.
void calcularRitmo() {
  ritmoHz = ritmoQual = 0; ritmoEixo = -1; if (ritmoQtd < TAM_RITMO) return;
  float med[3] = {0, 0, 0}, var[3] = {0, 0, 0};
  for (int e = 0; e < 3; e++) { for (int i = 0; i < TAM_RITMO; i++) med[e] += sinalRitmo[e][i]; med[e] /= TAM_RITMO;
    for (int i = 0; i < TAM_RITMO; i++) { float d = sinalRitmo[e][i] - med[e]; var[e] += d * d; } var[e] /= TAM_RITMO; }
  int e = 0; if (var[1] > var[e]) e = 1; if (var[2] > var[e]) e = 2;
  if (var[e] < RITMO_VAR_MIN) return;
  constexpr int LAG_MIN = (int)(FREQ_HZ / 3.0f), LAG_MAX = (int)(FREQ_HZ / 0.4f);
  float r[LAG_MAX + 2]; const float *s = sinalRitmo[e];
  for (int lag = LAG_MIN - 1; lag <= LAG_MAX + 1; lag++) {
    float ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < TAM_RITMO - lag; i++) { float a = s[(ritmoPos + i) % TAM_RITMO] - med[e], b = s[(ritmoPos + i + lag) % TAM_RITMO] - med[e]; ab += a * b; aa += a * a; bb += b * b; }
    r[lag] = ab / sqrtf(aa * bb + 1e-5f);
  }
  float minAte = 1; int melhor = -1;
  for (int lag = LAG_MIN; lag <= LAG_MAX; lag++) {
    minAte = fminf(minAte, r[lag]);
    if (r[lag] > r[lag - 1] && r[lag] >= r[lag + 1] && r[lag] > 0.3f && r[lag] - minAte > 0.15f) { melhor = lag; break; }
  }
  if (melhor < 0) { for (int lag = LAG_MIN; lag <= LAG_MAX; lag++) if (melhor < 0 || r[lag] > r[melhor]) melhor = lag; }
  ritmoQual = fmaxf(0, r[melhor]); ritmoHz = (float)FREQ_HZ / melhor; ritmoEixo = e;
}
// API: calcularCandidato — Combina sinais de movimento e orientação para selecionar classe candidata.
uint8_t calcularCandidato() {
  uint16_t p5 = 0; for (int i = 0; i < 5; i++) p5 += passos5[i];
  if (estadoAtual == 2 || p5 >= 3) return C_LOCOMOCAO;
  if (cabecaBaixa && estadoAtual != 0) return C_CABECA_BAIXA;
  if (ritmoValido() && !cabecaBaixa) return C_RITMO;
  if (estadoAtual == 0) return C_PARADO;
  return C_OUTRO;
}
// API: registrarResumoAgregado — Calcula e enfileira resumo periódico para CSV e histórico.
void registrarResumoAgregado() {
  uint8_t estM = 0, candM = 0;
  for (int i = 1; i < 3; i++) if (aggEst[i] > aggEst[estM]) estM = i;
  for (int i = 1; i < N_CAND; i++) if (aggCand[i] > aggCand[candM]) candM = i;
  char l[230];
  snprintf(l, sizeof(l), "%lu,%llu,%llu,%s,%.4f,%.2f,%.3f,%.1f,%.1f,%.1f,%u,%u,%.2f,%.2f,%s,%s,%s,%u,%u,%u\n",
           (unsigned long)sessao, (unsigned long long)ms64DeAmostra(tJanela), (unsigned long long)epochDeAmostra(tJanela), ANIMAL_ID, aggAcc / aggN, aggGiro / aggN, aggJerk / aggN,
           pitchAtual, rollAtual, cabecaGraus, aggCabeca, aggPassos, ritmoHz, ritmoQual, estadoNome(estM), CAND_NOME[candM], ROTULOS[rotuloProc], aggEst[0], aggEst[1], aggEst[2]);
  if (!resumoCheio) anexar(A_RESUMO, l);
  aggN = 0; aggAcc = aggGiro = aggJerk = 0; aggPassos = aggCabeca = 0; memset(aggEst, 0, sizeof(aggEst)); memset(aggCand, 0, sizeof(aggCand));
}
// API: fimJanela — Fecha a janela usando timestamp da amostra, não a hora do loop.
void fimJanela(uint32_t tms) {   // tms = carimbo da ultima amostra da janela
  if (!nJanela) return;
  tJanela = tms;
  const float n = nJanela;
  accAtual = sqrtf(somaAcc2 / n); giroAtual = sqrtf(somaGiro2 / n); jerkAtual = somaJerk / n;
  float gm[3] = {somaGrav[0] / n, somaGrav[1] / n, somaGrav[2] / n};
  for (int i = 0; i < 3; i++) { gravRec[gravRecPos][i] = gm[i]; }
  gravRecPos = (gravRecPos + 1) % 3; if (gravRecQtd < 3) gravRecQtd++;
  pitchAtual = atan2f(-gm[0], sqrtf(gm[1] * gm[1] + gm[2] * gm[2])) * 180.0f / PI; rollAtual = atan2f(gm[1], gm[2]) * 180.0f / PI;
  calcularCabeca(gm);
  uint8_t anterior = estadoAtual;
  if (accAtual < LIM_PARADO_ACC && giroAtual < LIM_PARADO_GIRO) estadoAtual = 0;
  else if (accAtual >= LIM_INTENSO_ACC || giroAtual >= LIM_INTENSO_GIRO) estadoAtual = 2;
  else estadoAtual = 1;
  if (anterior != estadoAtual && anterior <= 2) evento("ESTADO", estadoNome(estadoAtual));
  secsEstados[estadoAtual]++;
  if (estadoAtual == 0) { if (!paradoAtivo) { paradoAtivo = true; inicioParado = ms32(); } segParadoAtual = (ms32() - inicioParado) / 1000; } else { paradoAtivo = false; inicioParado = 0; segParadoAtual = 0; }
  if (estadoAtual == 0 && segParadoAtual >= 10 && !calibAtiva) {
    bool estavel = true; float med[3];
    for (int i = 0; i < 3; i++) { med[i] = somaGr[i] / n; if (somaGr2[i] / n - med[i] * med[i] > 0.36f) estavel = false; }
    if (estavel) { for (int i = 0; i < 3; i++) biasG[i] += 0.05f * (med[i] - biasG[i]); if (++autoZeroN % 600 == 0) salvarBias(); }
  }
  passos5[passos5Pos] = passosJanela; passos5Pos = (passos5Pos + 1) % 5;
  calcularRitmo();
  candAtual = calcularCandidato();
  recentSum -= recentMotion[recentPos]; recentMotion[recentPos] = (estadoAtual != 0) ? 1 : 0; recentSum += recentMotion[recentPos]; recentPos = (recentPos + 1) % 60;
  hist[histPos] = {(uint32_t)(ms64DeAmostra(tms) / 1000ULL), accAtual, giroAtual, candAtual}; histPos = (histPos + 1) % TAM_HIST; if (histQtd < TAM_HIST) histQtd++; seq++;
  if (!horaIniciada) iniciarHora(tms); else if (chaveHoraDe(tms) != horaAtual.chave) trocarHora(tms);
  horaAtual.seg[candAtual]++; horaAtual.segEst[estadoAtual]++; horaAtual.validos++; horaAtual.passos += passosJanela; if (cabecaBaixa) horaAtual.cabecaBaixaS++;
  segCandTotal[candAtual]++;
  checkpointRTC();
  if (++janelasDesdeCheckpoint >= CHECKPOINT_S) salvarCheckpointHora();
  aggN++; aggAcc += accAtual; aggGiro += giroAtual; aggJerk += jerkAtual; aggPassos += passosJanela; if (cabecaBaixa) aggCabeca++; aggEst[estadoAtual]++; aggCand[candAtual]++;
  if (aggN >= RESUMO_S) registrarResumoAgregado();
  desenharOLED();
  somaAcc2 = somaGiro2 = somaJerk = 0; for (int i = 0; i < 3; i++) { somaGrav[i] = somaGr[i] = somaGr2[i] = 0; }
  nJanela = 0; passosJanela = 0;
}

// ======================= BRUTO BINARIO =======================
// API: flushRaw — Despeja buffer de registros binários; TODO: validar bytes escritos antes de descartar buffer.
void flushRaw() { if (rawBufN && arquivoRaw) arquivoRaw.write((const uint8_t *)rawBuf, rawBufN * sizeof(RawReg)); rawBufN = 0; }
// API: pararRaw — Finaliza sessão bruta respeitando instante de parada e fila pendente.
void pararRaw(const char *motivo) { if (!rawAtivo) return; rawAtivo = false; flushRaw(); if (arquivoRaw) { arquivoRaw.flush(); arquivoRaw.close(); } evento("RAW_STOP", motivo); }
// Marcador de relogio na LINHA DO TEMPO: o ajuste vale a partir do instante T (ms32 do clique) e o
// registro e escrito no bruto antes da primeira amostra com carimbo >= T (ordem cronologica).
bool marcadorPendente = false; uint32_t marcadorT = 0; uint64_t marcadorEpoch = 0;
// API: marcadorRelogioRaw — Grava evento de sincronização do relógio na série binária.
void marcadorRelogioRaw() {
  if (!rawAtivo || !relogioOk) return;
  marcadorT = ms32(); marcadorEpoch = epochMs(); marcadorPendente = true;   // mesmo instante (<1 ms)
}
// API: escreverMarcadorSeDevido — Insere marcador pendente na posição temporal das amostras.
void escreverMarcadorSeDevido(uint32_t tms) {
  if (!marcadorPendente || (int32_t)(tms - marcadorT) < 0) return;
  if (rawBufN >= 100) flushRaw();
  RawReg &r = rawBuf[rawBufN++]; memset(&r, 0, sizeof(r)); r.ms = marcadorT; memcpy(r.v, &marcadorEpoch, sizeof(marcadorEpoch)); r.flags = 2;
  marcadorPendente = false;
}
// API: iniciarRaw — Inicializa cabeçalho binário versionado e os limites de duração da captura.
bool iniciarRaw(uint32_t minutos) {   // fim da captura = rawFimMs no relogio DAS AMOSTRAS
  if (!fsOk || !enderecoMPU) return false;
  pararRaw("REINICIO");
  if (LittleFS.exists("/raw.bin")) LittleFS.remove("/raw.bin");
  if (LittleFS.exists("/raw.csv")) LittleFS.remove("/raw.csv");
  if (!espacoDisponivel(60000)) return false;
  if (minutos < 1) minutos = 1;
  if (minutos > RAW_MAX_MIN) minutos = RAW_MAX_MIN;
  size_t livre = LittleFS.totalBytes() - LittleFS.usedBytes() - 60000;
  uint32_t maxS = livre / (sizeof(RawReg) * FREQ_HZ); if (minutos * 60 > maxS) minutos = (maxS / 60) ? (maxS / 60) : 1;
  arquivoRaw = LittleFS.open("/raw.bin", FILE_WRITE); if (!arquivoRaw) return false;
  RawCab c; memset(&c, 0, sizeof(c)); memcpy(c.magic, "SCR3", 4); c.versao = 3; c.ms64Inicio = ms64(); c.hz = FREQ_HZ; c.sessao = sessao; c.accLsb = ACC_LSB; c.gyrLsb = GYR_LSB;
  c.relogio = 0; strlcpy(c.animal, ANIMAL_ID, sizeof(c.animal)); for (int i = 0; i < 3; i++) c.bias[i] = biasG[i];
  arquivoRaw.write((const uint8_t *)&c, sizeof(c));
  rawCont = 0; rawBufN = 0; rawInicio = ms32(); rawFimMs = rawInicio + minutos * 60000UL; rawAtivo = true; marcadorPendente = false;
  marcadorRelogioRaw();
  char d[32]; snprintf(d, sizeof(d), "CAPTURA_%lu_MIN", (unsigned long)minutos); evento("RAW_START", d); return true;
}
// API: registrarRaw — Acrescenta amostra ao binário e respeita o limite pelo tempo de aquisição.
void registrarRaw(const Leitura &v, uint32_t tms) {
  if (!rawAtivo) return;
  if ((int32_t)(tms - rawFimMs) >= 0) { pararRaw("LIMITE_TEMPO"); return; }  // pelo horario da amostra
  if ((int32_t)(tms - rawInicio) < 0) return;   // amostra anterior ao clique em "Gravar" (ainda na fila)
  escreverMarcadorSeDevido(tms);
  RawReg &r = rawBuf[rawBufN++]; r.ms = tms; for (int i = 0; i < 6; i++) r.v[i] = v.r[i]; r.rotulo = rotuloProc; r.flags = lacunaPendente ? 1 : 0; lacunaPendente = false;
  rawCont++;
  if (rawBufN >= 100) { if (!espacoDisponivel(sizeof(rawBuf) + 4096)) { pararRaw("LIMITE_ESPACO"); return; } flushRaw(); }
}

// ======================= PROCESSAMENTO (loop) =======================
// API: registrarPico — Agrupa picos consecutivos para reduzir falsos passos isolados.
void registrarPico(uint32_t tms) {
  // Passo so conta dentro de um "trem" regular: >= PASSO_TREM_MIN picos com intervalos plausiveis.
  uint32_t dt = tms - ultimoPico; ultimoPico = tms;
  if (tremLen > 0 && dt >= PASSO_INT_MIN_MS && dt <= PASSO_INT_MAX_MS) tremLen = tremLen < 250 ? tremLen + 1 : tremLen;
  else { picosDescartados += passosPendentes; passosPendentes = 0; tremLen = 1; }
  if (tremLen >= PASSO_TREM_MIN) { uint16_t novos = passosPendentes + 1; passosPendentes = 0; passosJanela += novos; passosTotal += novos; }
  else passosPendentes++;
}
// API: processarAmostra — Pipeline principal da amostra: rótulo, filtros, métricas, janela e gravação.
void processarAmostra(const Leitura &v, uint32_t tms) {
  rotuloEm(tms);   // rotulo vigente NO INSTANTE da amostra
  float am = sqrtf(v.ax * v.ax + v.ay * v.ay + v.az * v.az);
  if (calibAtiva) {
    const float g[3] = {v.gx, v.gy, v.gz}; for (int i = 0; i < 3; i++) { calS[i] += g[i]; calS2[i] += g[i] * g[i]; }
    calAmin = fminf(calAmin, am); calAmax = fmaxf(calAmax, am); if (++calibN >= FREQ_HZ * 3) finalizarCalibGiro();
  }
  if (primeiroDado) { grav[0] = v.ax; grav[1] = v.ay; grav[2] = v.az; primeiroDado = false; }
  if (jerkReset) { antA[0] = v.ax; antA[1] = v.ay; antA[2] = v.az; jerkReset = false; }
  const float a = .94f; const float acc[3] = {v.ax, v.ay, v.az};
  float d[3];
  for (int i = 0; i < 3; i++) { grav[i] = a * grav[i] + (1 - a) * acc[i]; d[i] = acc[i] - grav[i]; somaGrav[i] += grav[i]; }
  const float graw[3] = {v.gx, v.gy, v.gz}; float g[3];
  for (int i = 0; i < 3; i++) { g[i] = graw[i] - biasG[i]; somaGr[i] += graw[i]; somaGr2[i] += graw[i] * graw[i]; }
  if (am > IMPACTO_G && ms32() - ultimaBatidaMs > 2000) { ultimaBatidaMs = ms32(); eventosImpacto++; if (horaIniciada) horaAtual.impactos++; evento("PICO_ACEL", "MAIOR_2_5G"); }
  float dm2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  somaAcc2 += dm2; somaGiro2 += g[0] * g[0] + g[1] * g[1] + g[2] * g[2];
  float j2 = 0; for (int i = 0; i < 3; i++) { float x = acc[i] - antA[i]; j2 += x * x; antA[i] = acc[i]; }
  somaJerk += sqrtf(j2) * FREQ_HZ;
  passoLp = 0.6f * passoLp + 0.4f * sqrtf(dm2);
  if (!passoAcima && passoLp > LIM_PASSO_ON && tms - ultimoPico >= PASSO_INT_MIN_MS) { passoAcima = true; registrarPico(tms); }
  else if (passoAcima && passoLp < LIM_PASSO_OFF) passoAcima = false;
  for (int e = 0; e < 3; e++) sinalRitmo[e][ritmoPos] = g[e];
  ritmoPos = (ritmoPos + 1) % TAM_RITMO; if (ritmoQtd < TAM_RITMO) ritmoQtd++;
  registrarRaw(v, tms);
  tUltimaAmostra = tms;
  if (++nJanela >= FREQ_HZ) fimJanela(tms);
}
#if HABILITAR_DEBUG
// API: filaLiberada — Controla atraso artificial apenas na bancada, ou libera a fila sempre em produção.
bool filaLiberada() { if (atrasoAtivo && ms32() - atrasoInicio < atrasoDur) return false; atrasoAtivo = false; return true; }
#else
// API: filaLiberada — Controla atraso artificial apenas na bancada, ou libera a fila sempre em produção.
inline bool filaLiberada() { return true; }   // campo: processamento SEMPRE incondicional
#endif
// API: servicoFila — Consome a fila produzida pela tarefa de aquisição, limitando o trabalho por iteração.
void servicoFila(uint16_t maximo) {  // consome amostras da tarefa do sensor
  Amostra s;
  while (maximo-- && xQueueReceive(filaAmostras, &s, 0) == pdTRUE) {
    if (s.flags & 1) { lacunaPendente = true; jerkReset = true; ritmoQtd = 0; }
    Leitura v; memcpy(v.r, s.r, sizeof(v.r));
    v.ax = s.r[0] / ACC_LSB; v.ay = s.r[1] / ACC_LSB; v.az = s.r[2] / ACC_LSB; v.gx = s.r[3] / GYR_LSB; v.gy = s.r[4] / GYR_LSB; v.gz = s.r[5] / GYR_LSB;
    processarAmostra(v, s.t);
  }
}
// API: verificarSensor — Converte contadores de erro/reconexão da tarefa em eventos consultáveis.
void verificarSensor() {  // eventos gerados a partir dos contadores da tarefa (a tarefa nao escreve arquivos)
  if (mpuInicios != vistoInicios) { vistoInicios = mpuInicios; primeiroDado = true; jerkReset = true; ritmoQtd = 0; nJanela = 0; evento("MPU", vistoInicios > 1 ? "RECONECTADO_FIFO" : "DETECTADO_FIFO"); iniciarCalibGiro(); }
  if (i2cFalhas != vistoFalhas) { vistoFalhas = i2cFalhas; estadoAtual = 3; evento("I2C", "ERRO_LEITURA_SENSOR_INTERROMPIDO"); } // bruto continua; lacuna marcada no retorno
  if (fifoLacunas != vistoLacunas) { vistoLacunas = fifoLacunas; evento("FIFO", "OVERFLOW_LACUNA"); }
  if (filaDescartadas != vistoDescartadas) { vistoDescartadas = filaDescartadas; evento("FILA", "CHEIA_AMOSTRAS_DESCARTADAS"); }
  if (!enderecoMPU) estadoAtual = 3;
}

// ======================= RELOGIO =======================
// API: ajustarRelogio — Atualiza offset de hora absoluta e prepara marcador temporal para o arquivo bruto.
void ajustarRelogio(double ms, const char *origem) {
  bool antes = relogioOk;
  relogioOffsetMs = (int64_t)ms - (int64_t)ms64(); relogioOk = true; relogioOrigem = origem;
  struct timeval tv; tv.tv_sec = (time_t)(ms / 1000.0); tv.tv_usec = (suseconds_t)(fmod(ms, 1000.0) * 1000.0); settimeofday(&tv, nullptr); // RTC interno
  marcadorRelogioRaw();
  if (!antes) { evento("RELOGIO", "SINCRONIZADO"); if (horaIniciada) trocarHora(tUltimaAmostra); calcularIndice(chaveHoraAgora()); }
}
// API: relogioDoRTC — Tenta restaurar a referência de hora após reset sem interrupção de energia.
void relogioDoRTC() {  // apos reset por software/watchdog o RTC interno costuma manter a hora
  struct timeval tv; gettimeofday(&tv, nullptr);
  if (tv.tv_sec > 1700000000L && tv.tv_sec < 4100000000L) {
    relogioOffsetMs = (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000 - (int64_t)ms64(); relogioOk = true; relogioOrigem = "RTC apos reinicio";
  }
}

// ======================= BATERIA / BOTAO / WI-FI =======================
// API: lerVBAT — Lê bateria via ADC e classifica estado; leituras ausentes não equivalem a bateria OK.
void lerVBAT() {
  if (bateriaLida && ms32() - ultimaLeituraBateria < 10000) return;
  bateriaLida = true;
  ultimaLeituraBateria = ms32();
  digitalWrite(PIN_VBAT_EN, HIGH); delay(2);
  uint32_t soma = 0; for (int i = 0; i < 8; i++) { soma += analogReadMilliVolts(PIN_VBAT_ADC); delay(1); }
  digitalWrite(PIN_VBAT_EN, LOW);
  const float leitura = (soma / 8.0f) * 4.9f / 1000.0f; bateriaLeituraBruta = leitura;
  // < 2,0 V ou > 4,5 V: leitura invalida (sem bateria, ADC/divisor). 2,0-4,5 V: aceita, inclusive CRITICA.
  if (leitura < 2.0f || leitura > 4.5f) { bateriaEstado = "SEM_LEITURA"; return; }
  bool salto = bateriaVolts > 0 && fabsf(leitura - bateriaVolts) > 0.4f;  // queda brusca nao e suavizada
  bateriaVolts = (bateriaVolts < 0.0f || salto) ? leitura : (0.8f * bateriaVolts + 0.2f * leitura);
  const char *novo = bateriaVolts < 3.3f ? "CRITICA" : (bateriaVolts < 3.55f ? "BAIXA" : "OK");
  if (strcmp(novo, bateriaEstado) != 0 && strcmp(novo, "OK") != 0) evento("BATERIA", novo);
  bateriaEstado = novo;
}
// API: ligarWifi — Inicializa ponto de acesso local para dashboard e APIs de pesquisa.
void ligarWifi() {
  if (wifiLigado) return;
  WiFi.persistent(false); WiFi.mode(WIFI_AP); WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_NOME, AP_SENHA); web.begin(); wifiLigado = true; ultimoCliente = ms32(); evento("WIFI", "LIGADO");
}
// API: desligarWifi — Para painel e AP para economizar energia quando configurado.
void desligarWifi() { if (!wifiLigado) return; web.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); wifiLigado = false; evento("WIFI", "DESLIGADO"); }
// API: servicoBotao — Trata botão físico sem bloquear aquisição; também pode disparar marcador de vídeo.
void servicoBotao() {
  bool p = digitalRead(PIN_BTN) == LOW; uint32_t agora = ms32();
  if (p && !btnPress) { btnPress = true; btnIni = agora; btnLongo = false; acordarOLED(); }
  if (p && btnPress && !btnLongo && agora - btnIni > 2000) { btnLongo = true; if (wifiLigado) desligarWifi(); else ligarWifi(); desenharOLED(); }
  if (!p && btnPress) { btnPress = false; if (!btnLongo && agora - btnIni > 40) marcarSync("BOTAO"); }
}

// ======================= LORA (opcional) =======================
#if HABILITAR_LORA
// API: iniciarLoRa — Inicializa transceptor e parâmetros de rádio compatíveis com a base v1.4.
void iniciarLoRa() {
  ultimaInitLora = ms32(); loraIrq = false;
  SPI.begin(9, 11, 10, 8);
  int16_t rc = loraBeginHeltec(radio, LORA_FREQ_MHZ, LORA_POTENCIA_DBM);
  loraPronto = (rc == RADIOLIB_ERR_NONE); loraFase = L_OCIOSO;
  if (loraPronto) { radio.setDio1Action(loraIsr); radio.sleep(); }
  Serial.printf("LoRa SX1262: %d\n", (int)rc);
}
// API: loraRecuperar — Reinicializa transceptor após timeout e registra diagnóstico.
void loraRecuperar(const char *motivo) { loraTravN++; evento("LORA", motivo); iniciarLoRa(); }
// API: loraEnviar — Transmite pacote acumulado e gerencia confirmação pelo protocolo v5.
void loraEnviar() {
  loraPend.tentativa = loraTent; loraIrq = false;
  if (radio.startTransmit((const uint8_t *)&loraPend, sizeof(loraPend)) != RADIOLIB_ERR_NONE) { loraRecuperar("TX_NAO_INICIOU"); return; }
  loraFase = L_TX; loraTFase = ms32(); loraTxN++;
}
// API: montarPacoteLoRa — Empacota totais acumulados, boot ID e status para tolerar perdas de pacotes.
void montarPacoteLoRa() {
  PacoteLoRa &p = loraPend; memset(&p, 0, sizeof(p)); p.magic = 0xC0; p.versao = SC_LORA_VERSAO; strncpy(p.animal, ANIMAL_ID, sizeof(p.animal));
  p.seq = ++seqTx; p.bat_mv = bateriaVolts > 0 ? (uint16_t)(bateriaVolts * 1000) : 0; p.temp_c = (int8_t)constrain((int)lroundf(chipTemp), -100, 120);
  p.estado = estadoAtual; p.candidato = candAtual; p.boot_id = bootId;
  for (int i = 0; i < N_CAND; i++) p.seg_total[i] = segCandTotal[i];   // ACUMULADOS desde o boot: nada se perde se um pacote falhar
  p.passos_hora = horaIniciada ? (uint16_t)min<uint32_t>(65535, horaAtual.passos) : 0;
  p.indice_x10 = indiceAtividade < 0 ? 255 : (uint8_t)min(254.0f, indiceAtividade * 10);
  p.flags = (alertaAtivo ? 1 : 0) | (rawAtivo ? 2 : 0) | (relogioOk ? 4 : 0) | (refNormal ? 8 : 0);
  p.uptime_s = (uint32_t)(ms64() / 1000ULL); p.passos_total = passosTotal;
}
// API: servicoLoRa — Máquina de estados do LoRa: janela de ACK, timeout e retransmissões.
void servicoLoRa() {
  uint32_t agora = ms32();
  if (!loraPronto) { if (agora - ultimaInitLora > 60000) iniciarLoRa(); return; }
  if (loraIrq) {
    loraIrq = false;
    if (loraFase == L_TX) { radio.finishTransmit(); radio.startReceive(); loraFase = L_RX_ACK; loraTFase = agora; }
    else if (loraFase == L_RX_ACK) {
      AckLoRa a; memset(&a, 0, sizeof(a)); size_t n = radio.getPacketLength(); int16_t rc = radio.readData((uint8_t *)&a, sizeof(a));
      if (rc == RADIOLIB_ERR_NONE && n == sizeof(a) && a.magic == 0xC1 && a.versao == SC_LORA_VERSAO && a.seq == loraPend.seq && strncmp(a.animal, loraPend.animal, 10) == 0) {
        loraAckN++; loraUltRssiBase = a.rssi_base; radio.sleep(); loraFase = L_OCIOSO;
      } else radio.startReceive(); // pacote de outro colar: continua ouvindo
    }
  }
  if (loraFase == L_TX && agora - loraTFase > 3000) { loraRecuperar("TX_TRAVADO_REINICIADO"); return; }
  if (loraFase == L_RX_ACK && agora - loraTFase > LORA_JANELA_ACK_MS) {
    radio.standby();
    if (++loraTent <= LORA_RETRIES) { loraFase = L_ESPERA; loraProxTent = agora + 200 + esp_random() % 1300; }
    else { loraFalhasN++; radio.sleep(); loraFase = L_OCIOSO; }
  }
  if (loraFase == L_ESPERA && (int32_t)(agora - loraProxTent) >= 0) loraEnviar();
  if (loraFase == L_OCIOSO && agora - ultimaTx >= LORA_INTERVALO_S * 1000UL && estadoAtual <= 2) { ultimaTx = agora; montarPacoteLoRa(); loraTent = 0; loraEnviar(); }
}
#endif

// ======================= HTTP =======================
// API: apiStatus — Produz JSON de telemetria do painel, inclusive contadores de lacunas e armazenamento.
void apiStatus() {
  static char buf[2400]; Json j(buf, sizeof(buf)); char tmp[8];
  j.u("seq", seq); j.u("sessao", sessao); j.s("animal", ANIMAL_ID); j.u("uptime_s", ms64() / 1000ULL);
  j.s("estado", estadoNome(estadoAtual)); j.s("candidato", CAND_NOME[candAtual]); j.i("cand_i", candAtual);
  j.f("acc", accAtual, 4); j.f("giro", giroAtual, 2); j.f("jerk", jerkAtual, 3); j.f("pitch", pitchAtual, 1); j.f("roll", rollAtual, 1);
  j.f("cabeca", cabecaGraus, 1); j.bo("cabeca_baixa", cabecaBaixa); j.i("cabeca_ref", refNormal ? (refBaixa ? 2 : 1) : 0);
  uint16_t p5 = 0; for (int i = 0; i < 5; i++) p5 += passos5[i];
  j.u("passos_5s", p5); j.u("passos_hora", horaIniciada ? horaAtual.passos : 0); j.u("passos_total", passosTotal); j.u("picos_descartados", picosDescartados);
  j.f("ritmo_hz", ritmoHz, 3); j.f("ritmo_qual", ritmoQual, 2); j.bo("ritmo_ok", ritmoValido());
  if (indiceAtividade < 0) j.nul("indice"); else j.f("indice", indiceAtividade, 2);
  j.s("indice_motivo", indiceMotivo); j.bo("alerta", alertaAtivo); j.u("horas_hist", horasQtd);
  j.f("temp_chip_c", chipTemp, 2);
  if (bateriaVolts < 0) j.nul("bateria_v"); else j.f("bateria_v", bateriaVolts, 3);
  j.bo("bateria_baixa", bateriaVolts > 0 && bateriaVolts < 3.55f); j.s("bateria_estado", bateriaEstado); j.f("bateria_bruta_v", bateriaLeituraBruta, 2);
  j.f("movimento_min", recentSum * 100.0f / 60.0f, 1); j.u("parado_s", secsEstados[0]); j.u("leve_s", secsEstados[1]); j.u("intenso_s", secsEstados[2]); j.u("parado_atual_s", segParadoAtual);
  j.bo("sensor", enderecoMPU != 0); snprintf(tmp, sizeof(tmp), "0x%02X", enderecoMPU); j.s("i2c", tmp);
  j.u("fifo_lacunas", fifoLacunas); j.u("i2c_falhas", i2cFalhas); j.u("fila_max", filaMaxUso); j.u("fila_cap", FILA_AMOSTRAS); j.u("fila_descartadas", filaDescartadas);
  j.bo("fs", fsOk); j.u("fs_livre", fsOk ? LittleFS.totalBytes() - LittleFS.usedBytes() : 0); j.bo("resumo_cheio", resumoCheio);
  j.bo("raw", rawAtivo); j.u("raw_count", rawCont); j.u("raw_s", rawAtivo ? (ms32() - rawInicio) / 1000 : 0); j.u("raw_lim_s", rawAtivo ? (rawFimMs - rawInicio) / 1000 : 0);
  j.s("rotulo", ROTULOS[rotuloAtual]);
#if HABILITAR_DEBUG
  { char t[64]; snprintf(t, sizeof(t), "debug%s%s%s", atrasoAtivo ? " fila_parada" : "", flashCheiaTeste ? " flash_cheia" : "", TESTE_HORA_CURTA ? " hora=2min" : ""); j.s("teste", t); }
#else
  j.s("teste", "");
#endif
  j.s("rotulo_dados", ROTULOS[rotuloProc]); j.u("boot_id", bootId);  j.u("eventos", eventosImpacto); j.u("autozero", autoZeroN); j.u("ck_falhas", ckFalhas); j.u("horas_nao_gravadas", horasNaoGravadas); j.u("horas_pendentes", pendQtd); j.u("horas_perdidas", horasPerdidas); j.bo("hora_pendente", pendQtd > 0); j.s("calib", calibMsg);
  j.bo("relogio", relogioOk); j.s("relogio_origem", relogioOrigem); j.u("epoch_ms", epochMs()); j.u("heap", ESP.getFreeHeap()); j.u("wifi_clientes", WiFi.softAPgetStationNum()); j.u("sync_n", syncN);
#if HABILITAR_LORA
  j.i("lora", loraPronto ? 1 : -2); j.u("lora_tx", loraTxN); j.u("lora_ack", loraAckN); j.u("lora_falhas", loraFalhasN); j.u("lora_trav", loraTravN); j.i("lora_rssi_base", loraUltRssiBase);
#else
  j.i("lora", -1);
#endif
  web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json; charset=utf-8", j.fim());
}
// API: apiHistory — Envia histórico recente de atividade em JSON.
void apiHistory() {
  web.setContentLength(CONTENT_LENGTH_UNKNOWN); web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json", ""); web.sendContent("[");
  char row[96];
  for (int i = 0; i < histQtd; i++) { int k = (histPos + TAM_HIST - histQtd + i) % TAM_HIST;
    snprintf(row, sizeof(row), "%s{\"t\":%lu,\"a\":%.4f,\"g\":%.2f,\"c\":%u}", i ? "," : "", (unsigned long)hist[k].t, hist[k].acc, hist[k].giro, hist[k].cand); web.sendContent(row); }
  web.sendContent("]"); web.sendContent("");
}
// API: linhaHoraJson — Serializa uma hora histórica em formato legível para o dashboard.
void linhaHoraJson(char *row, size_t n, const Hora &h, bool virgula) {
  snprintf(row, n, "%s{\"k\":%lu,\"r\":%u,\"e\":%lu,\"s\":[%u,%u,%u,%u,%u],\"p\":%lu,\"b\":%u,\"v\":%u}", virgula ? "," : "", (unsigned long)h.chave, (h.chave & 0x80000000UL) ? 0u : 1u,
           (unsigned long)h.epochIni, h.seg[0], h.seg[1], h.seg[2], h.seg[3], h.seg[4], (unsigned long)h.passos, h.cabecaBaixaS, h.validos);
}
// API: apiHours — Retorna série agregada de horas para visualização.
void apiHours() {
  int pedido = web.hasArg("n") ? constrain((int)web.arg("n").toInt(), 1, TAM_HORAS) : 24;
  int n = min<int>(pedido - (horaIniciada ? 1 : 0), horasQtd); if (n < 0) n = 0;
  web.setContentLength(CONTENT_LENGTH_UNKNOWN); web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json", ""); web.sendContent("[");
  char row[160]; bool v = false;
  for (int i = horasQtd - n; i < horasQtd; i++) { linhaHoraJson(row, sizeof(row), horaIdx(i), v); v = true; web.sendContent(row); }
  if (horaIniciada) { linhaHoraJson(row, sizeof(row), horaAtual, v); web.sendContent(row); }
  web.sendContent("]"); web.sendContent("");
}
// API: podeBaixar — Evita downloads incompatíveis com uma sessão de gravação ainda em andamento.
bool podeBaixar() { if (rawAtivo) { web.send(409, "text/plain", "Pare a gravacao bruta antes de baixar arquivos"); return false; } if (!fsOk) { web.send(503, "text/plain", "LittleFS indisponivel"); return false; } return true; }
// API: enviarArquivo — Entrega arquivo do dispositivo via HTTP local.
void enviarArquivo(const char *path, const char *nome, const char *tipo) {
  if (!podeBaixar()) return;
  if (!LittleFS.exists(path)) { web.send(404, "text/plain", "Arquivo ainda indisponivel"); return; }
  File f = LittleFS.open(path, FILE_READ); if (!f) { web.send(500, "text/plain", "Erro abrindo arquivo"); return; }
  web.setContentLength(f.size()); web.sendHeader("Content-Disposition", String("attachment; filename=") + nome); web.sendHeader("Cache-Control", "no-store");
  arqBaixando = -1; for (int i = 0; i < 3; i++) if (strcmp(path, ARQ_PATH[i]) == 0) arqBaixando = i;
  web.send(200, tipo, ""); baixando = true; uint8_t buf[1024];
  while (f.available() && web.client().connected()) { size_t n = f.read(buf, sizeof(buf)); web.sendContent((const char *)buf, n); servicoFila(100); wdtReset(); }
  f.close(); baixando = false; arqBaixando = -1; esvaziarFila();
}
// API: enviarRawCSV — Converte registros binários em CSV com horários e indicador fora_de_ordem.
void enviarRawCSV() {
  if (!podeBaixar()) return;
  File f = LittleFS.open("/raw.bin", FILE_READ); RawCab c;
  if (!f || f.read((uint8_t *)&c, sizeof(c)) != sizeof(c) || memcmp(c.magic, "SCR3", 4) != 0) { if (f) f.close(); web.send(404, "text/plain", "Sem gravacao bruta v3"); return; }
  web.setContentLength(CONTENT_LENGTH_UNKNOWN); web.sendHeader("Content-Disposition", "attachment; filename=smartcollar_raw.csv"); web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/csv; charset=utf-8", "");
  web.sendContent("sessao,ms,epoch_ms,animal,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,rotulo_humano,lacuna_antes,fora_de_ordem\n");
  bool relogio = c.relogio; int64_t offset = c.offsetMs;   // atualizados pelos marcadores dentro do arquivo
  // Desembrulha o ms de 32 bits de cada registro em 64 bits (passos com sinal a partir do inicio)
  uint64_t ms64r = c.versao >= 3 ? c.ms64Inicio : 0; uint32_t baixo = (uint32_t)ms64r; bool primeiro = c.versao < 3;
  if (!relogio) {   // 1a passada: o 1o marcador vale tambem para as amostras anteriores a ele
    uint64_t m = ms64r; uint32_t b = baixo; bool p1 = primeiro; RawReg q;
    while (f.read((uint8_t *)&q, sizeof(q)) == sizeof(q)) {
      if (p1) { m = q.ms; b = q.ms; p1 = false; }
      m += (int64_t)(int32_t)(q.ms - b); b = q.ms;
      if (q.flags & 2) { uint64_t e; memcpy(&e, q.v, sizeof(e)); offset = (int64_t)e - (int64_t)m; relogio = true; break; }
      if ((f.position() & 0x3FFF) == 0) { servicoFila(100); wdtReset(); }
    }
    f.seek(sizeof(RawCab));
  }
  uint64_t ultimoMs = 0; uint32_t foraDeOrdem = 0;
  baixando = true; static char out[2400]; size_t o = 0; RawReg r[20];
  while (web.client().connected()) {
    int lidos = f.read((uint8_t *)r, sizeof(r)) / sizeof(RawReg); if (lidos <= 0) break;
    for (int k = 0; k < lidos; k++) {
      if (primeiro) { ms64r = r[k].ms; baixo = r[k].ms; primeiro = false; }
      ms64r += (int64_t)(int32_t)(r[k].ms - baixo); baixo = r[k].ms;
      if (r[k].flags & 2) { uint64_t e; memcpy(&e, r[k].v, sizeof(e)); offset = (int64_t)e - (int64_t)ms64r; relogio = true; continue; }
      unsigned long long ep = relogio ? (unsigned long long)((int64_t)ms64r + offset) : 0ULL;
      const bool fora = ultimoMs && ms64r <= ultimoMs; if (fora) foraDeOrdem++; ultimoMs = ms64r;
      o += snprintf(out + o, sizeof(out) - o, "%lu,%llu,%llu,%s,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%s,%u,%u\n", (unsigned long)c.sessao, (unsigned long long)ms64r, ep, c.animal,
                    r[k].v[0] / c.accLsb, r[k].v[1] / c.accLsb, r[k].v[2] / c.accLsb, r[k].v[3] / c.gyrLsb, r[k].v[4] / c.gyrLsb, r[k].v[5] / c.gyrLsb,
                    ROTULOS[r[k].rotulo < N_ROTULOS ? r[k].rotulo : 0], r[k].flags & 1, fora ? 1u : 0u);
      if (o > sizeof(out) - 140) { web.sendContent(out, o); o = 0; }
    }
    servicoFila(100); wdtReset();
  }
  if (o) web.sendContent(out, o);
  web.sendContent(""); f.close(); baixando = false; esvaziarFila();
  if (foraDeOrdem) { char d[40]; snprintf(d, sizeof(d), "EXPORT_FORA_DE_ORDEM_%lu", (unsigned long)foraDeOrdem); evento("RAW", d); }
}
// API: registrarRotas — Registra endpoints HTTP do painel, aquisição e depuração opcional.
void registrarRotas() {
  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html; charset=utf-8", HTML); });
  web.on("/api/status", HTTP_GET, apiStatus); web.on("/api/history", HTTP_GET, apiHistory); web.on("/api/hours", HTTP_GET, apiHours);
  web.on("/csv/summary", HTTP_GET, [] { enviarArquivo("/resumo.csv", "smartcollar_resumo.csv", "text/csv; charset=utf-8"); });
  web.on("/csv/events", HTTP_GET, [] { enviarArquivo("/eventos.csv", "smartcollar_eventos.csv", "text/csv; charset=utf-8"); });
  web.on("/csv/hours", HTTP_GET, [] { enviarArquivo("/horas.csv", "smartcollar_horas.csv", "text/csv; charset=utf-8"); });
  web.on("/csv/raw", HTTP_GET, enviarRawCSV);
  web.on("/bin/raw", HTTP_GET, [] { enviarArquivo("/raw.bin", "smartcollar_raw.bin", "application/octet-stream"); });
  web.on("/api/time", HTTP_POST, [] {
    if (!web.hasArg("ms")) { web.send(400, "text/plain", "Falta ms"); return; }
    double ms = atof(web.arg("ms").c_str()); if (ms < 1.7e12 || ms > 4.1e12) { web.send(400, "text/plain", "Hora invalida"); return; }
    ajustarRelogio(ms, "navegador"); web.send(200, "text/plain", "Relogio ajustado");
  });
  web.on("/api/calibrate", HTTP_POST, [] { if (!enderecoMPU) { web.send(409, "text/plain", "MPU indisponivel"); return; } querCalibrar = true; web.send(202, "text/plain", "Calibracao iniciada: mantenha IMOVEL 3 s. Resultado aparece no painel."); });
  web.on("/api/orient", HTTP_POST, [] {
    String t = web.arg("tipo");
    if (t == "limpar") { refNormal = refBaixa = false; prefs.putBool("rn", false); prefs.putBool("rb", false); evento("ORIENTACAO", "LIMPA"); web.send(200, "text/plain", "Referencias da cabeca apagadas"); return; }
    if (gravRecQtd < 3 || !enderecoMPU) { web.send(409, "text/plain", "Aguarde alguns segundos de dados"); return; }
    float m[3] = {0, 0, 0}; for (int k = 0; k < 3; k++) for (int i = 0; i < 3; i++) m[i] += gravRec[k][i] / 3; normalizar(m);
    if (t == "normal") { memcpy(gU, m, sizeof(gU)); refNormal = true; prefs.putBytes("gu", gU, sizeof(gU)); prefs.putBool("rn", true); evento("ORIENTACAO", "CABECA_NORMAL"); web.send(200, "text/plain", "Referencia NORMAL salva"); }
    else if (t == "baixa") {
      if (!refNormal) { web.send(409, "text/plain", "Calibre a cabeca NORMAL primeiro"); return; }
      float ang = acosf(fminf(1, fmaxf(-1, dot3(gU, m)))) * 180 / PI;
      if (ang < 15) { web.send(409, "text/plain", "Posicao muito parecida com a normal (<15 graus). Tente com a cabeca mais baixa."); return; }
      memcpy(gD, m, sizeof(gD)); refBaixa = true; prefs.putBytes("gd", gD, sizeof(gD)); prefs.putBool("rb", true); evento("ORIENTACAO", "CABECA_BAIXA");
      web.send(200, "text/plain", String("Referencia BAIXA salva (") + String(ang, 0) + " graus da normal)");
    } else web.send(400, "text/plain", "tipo = normal | baixa | limpar");
  });
  web.on("/api/raw/start", HTTP_POST, [] {
    uint32_t m = web.hasArg("min") ? web.arg("min").toInt() : 3;
    if (!iniciarRaw(m)) { web.send(507, "text/plain", "Sem LittleFS, sem sensor ou sem espaco."); return; }
    web.send(200, "text/plain", String("Captura iniciada: ate ") + String((rawFimMs - rawInicio) / 60000UL) + " min. Downloads bloqueados ate parar.");
  });
  web.on("/api/raw/stop", HTTP_POST, [] { if (rawAtivo) { rawFimMs = ms32(); servicoFila(FILA_AMOSTRAS); } pararRaw("USUARIO"); web.send(200, "text/plain", "Captura encerrada. Baixe o bruto."); });
  web.on("/api/label", HTTP_POST, [] {
    if (!web.hasArg("code")) { web.send(400, "text/plain", "Falta code"); return; }
    int i = web.arg("code").toInt(); if (i < 0 || i >= N_ROTULOS) { web.send(400, "text/plain", "Rotulo invalido"); return; }
    mudarRotulo((uint8_t)i); evento("ROTULO_HUMANO", ROTULOS[rotuloAtual]); web.send(200, "text/plain", String("Rotulo do observador: ") + ROTULOS[rotuloAtual]);
  });
  web.on("/api/sync", HTTP_POST, [] { marcarSync("PAINEL"); web.send(200, "text/plain", String("Marcador #") + String(syncN) + " salvo; LED piscando"); });
  web.on("/api/session/new", HTTP_POST, [] {
    pararRaw("NOVA_SESSAO"); if (!fsOk) { web.send(503, "text/plain", "LittleFS indisponivel; nada foi apagado"); return; }
    const char *apagar[] = {"/resumo.csv", "/eventos.csv", "/raw.bin", "/raw.csv", "/horas.csv", "/horas_antigo.csv", "/hora_atual.bin"}; for (auto p : apagar) LittleFS.remove(p);
    sessao = esp_random(); prefs.putUInt("sessao", sessao); rotuloAtual = 0; rotuloProc = 0; mudRotQtd = 0; rtcCk.magic = 0; pendQtd = 0; salvarPendNVS(); idsQtd = idsPos = 0; apagarCheckpoints(); memset(segCandTotal, 0, sizeof(segCandTotal)); histPos = histQtd = 0; seq = 0; recentPos = 0; recentSum = 0; memset(recentMotion, 0, sizeof(recentMotion));
    memset(secsEstados, 0, sizeof(secsEstados)); inicioParado = segParadoAtual = 0; eventosImpacto = 0; passosTotal = 0; picosDescartados = 0; resumoCheio = false;
    horasPos = horasQtd = 0; horaIniciada = false; temHoraAnterior = false; indiceAtividade = -1; alertaAtivo = false; aggN = 0;
    garantirCabecalho("/resumo.csv", CAB_RESUMO, "/resumo_antigo.csv"); garantirCabecalho("/eventos.csv", CAB_EVENTOS, "/eventos_antigo.csv"); garantirCabecalho("/horas.csv", CAB_HORAS, "/horas_antigo.csv");
    evento("NOVA_SESSAO", "RESET_LOGS"); web.send(200, "text/plain", "Nova sessao criada. Logs anteriores apagados.");
  });
#if HABILITAR_DEBUG
  web.on("/api/debug/atraso", HTTP_POST, [] {   // TESTE DE BANCADA: segura o processamento da fila
    long s = web.hasArg("s") ? web.arg("s").toInt() : 20; if (s < 0) s = 0; if (s > 50) s = 50;
    atrasoAtivo = true; atrasoInicio = ms32(); atrasoDur = s * 1000UL; char d[32]; snprintf(d, sizeof(d), "ATRASO_FILA_%lds", s); evento("TESTE", d);
    web.send(200, "text/plain", String("Fila parada por ") + String(s) + " s. Troque rotulos agora e confira no bruto.");
  });
  web.on("/api/debug/flash_cheia", HTTP_POST, [] {   // TESTE: toda gravacao em CSV falha (simula flash cheia)
    flashCheiaTeste = web.arg("on") == "1"; evento("TESTE", flashCheiaTeste ? "FLASH_CHEIA_ON" : "FLASH_CHEIA_OFF");
    web.send(200, "text/plain", flashCheiaTeste ? "Gravacoes CSV simulando falha" : "Gravacoes normais");
  });
#endif
  web.onNotFound([] { web.send(404, "text/plain", "Nao encontrado"); });
}

// ======================= SETUP / LOOP =======================
// API: carregarPrefs — Recupera configurações de calibração e preferências locais da NVS.
void carregarPrefs() {
  prefs.begin("sc3", false);
  if (prefs.getBool("bok", false)) { biasG[0] = prefs.getFloat("bgx", 0); biasG[1] = prefs.getFloat("bgy", 0); biasG[2] = prefs.getFloat("bgz", 0); strlcpy(calibMsg, "bias carregado da NVS", sizeof(calibMsg)); }
  refNormal = prefs.getBool("rn", false) && prefs.getBytes("gu", gU, sizeof(gU)) == sizeof(gU);
  refBaixa = refNormal && prefs.getBool("rb", false) && prefs.getBytes("gd", gD, sizeof(gD)) == sizeof(gD);
  sessao = prefs.getUInt("sessao", 0); if (!sessao) { sessao = esp_random(); prefs.putUInt("sessao", sessao); }
}
// API: setup — Inicialização: sensores, arquivos, checkpoints, rádio, painel e tarefas.
void setup() {
  if (CPU_80MHZ) setCpuFrequencyMhz(80);
  Serial.begin(115200); delay(300); Serial.println("\nSMART COLLAR RESEARCH v3.1.3");
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW); pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_VBAT_EN, OUTPUT); digitalWrite(PIN_VBAT_EN, LOW); analogReadResolution(12); lerVBAT();
  // Sensor primeiro: a tarefa comeca a guardar amostras enquanto o resto inicializa
  filaAmostras = xQueueCreate(FILA_AMOSTRAS, sizeof(Amostra));
  Wire.begin(PIN_MPU_SDA, PIN_MPU_SCL); Wire.setClock(100000);
  xTaskCreatePinnedToCore(tarefaSensor, "sensor", 4096, nullptr, 3, nullptr, 1);
  bootId = esp_random(); carregarPrefs(); relogioDoRTC(); iniciarOLED(); iniciarWDT();
  fsOk = LittleFS.begin(FORMATAR_FS_SE_FALHAR);
  if (fsOk) {
    garantirCabecalho("/resumo.csv", CAB_RESUMO, "/resumo_v2.csv"); garantirCabecalho("/eventos.csv", CAB_EVENTOS, "/eventos_v2.csv"); garantirCabecalho("/horas.csv", CAB_HORAS, "/horas_v312.csv");
    carregarHoras();
  } else Serial.println("LittleFS indisponivel: logs persistentes DESLIGADOS (veja Partition Scheme).");
  { char d[48]; snprintf(d, sizeof(d), "%s_BOOTID_%08lX", motivoReset(), (unsigned long)bootId); evento("BOOT", d); }
  if (relogioOk) evento("RELOGIO", "RECUPERADO_DO_RTC");
  carregarPendNVS(); if (pendQtd) { char d[32]; snprintf(d, sizeof(d), "FILA_DURAVEL_%u_HORAS", pendQtd); evento("HORA", d); ultimaTentPendente = ms32() - 60000; }
  restaurarCheckpointHora(); calcularIndice(chaveHoraAgora());
  registrarRotas(); ligarWifi();
#if HABILITAR_LORA
  iniciarLoRa();
#endif
  Serial.println("Painel: http://192.168.4.1  | Botao PRG: toque=SYNC, 2 s=Wi-Fi");
}
// API: loop — Laço de serviço não bloqueante: drena filas, publica dados, mantém rádio e watchdog.
void loop() {
  wdtReset();
  verificarSensor();
  if (filaLiberada()) servicoFila(400);
  if (wifiLigado) {
    web.handleClient();
    if (WIFI_AUTO_DESLIGA_MIN) { if (WiFi.softAPgetStationNum() > 0) ultimoCliente = ms32(); else if (ms32() - ultimoCliente > WIFI_AUTO_DESLIGA_MIN * 60000UL) desligarWifi(); }
  }
  servicoBotao(); servicoLedOled(); lerVBAT(); servicoPendente();
#if HABILITAR_LORA
  servicoLoRa();
#endif
  if (querCalibrar) { querCalibrar = false; iniciarCalibGiro(); }
  delay(5);
}
