/*
  SMART COLLAR BASE v1.4 - estacao-base LoRa (SEGUNDA Heltec WiFi LoRa 32 V3/V3.2)
  Para colares SmartCollar v3.1.2/v3.1.3 (protocolo LoRa v5). SmartCollar_lora.h IDENTICO ao do colar.
  Bibliotecas: RadioLib 7.x/8.x + U8g2. Antenas SEMPRE conectadas nas duas placas.

  v1.4 - o base.csv e a UNICA fonte de verdade (quarta revisao):
   - Cada linha leva os ACUMULADOS do colar (boot_id, seq, uptime, passos_total, seg_total x5) e um
     checksum proprio (crc). Nao existe mais arquivo de referencia separado: nao ha duas gravacoes
     para manter coerentes.
   - Ordem: grava a linha -> confere bytes -> SO ENTAO avanca a referencia em RAM e envia o ACK.
     Falha na gravacao = SEM ACK: o colar retransmite e a base tenta de novo (idempotente por seq).
     ACK passa a significar "gravado na flash", nao so "recebido pelo radio".
   - No boot, a referencia de cada colar e RECONSTRUIDA lendo base_antigo.csv + base.csv
     (linhas com crc invalido ou incompletas sao ignoradas; arquivo sem '\n' final e corrigido).
     Corte de energia em qualquer ponto: ou a linha esta no CSV (e vira referencia), ou nao esta
     (e o colar ainda nao recebeu ACK, entao retransmite / o proximo pacote cobre o intervalo).
   - Sem LittleFS: funciona so pela Serial, com ACK, e o painel avisa que nada e duravel.
*/
#include "SmartCollar_lora.h"
#include <SPI.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

#include "Base_config_local.h"  // Copie .example.h para este nome; nunca commite credenciais.
constexpr float LORA_FREQ_MHZ = 915.0f;   // igual ao colar
constexpr int8_t LORA_POTENCIA_DBM = 14;
constexpr uint32_t ACK_ATRASO_MS = 300;   // contado a partir da recepcao (inclui o tempo de gravacao)
constexpr int MAX_COLARES = 16;
constexpr size_t LOG_MAX_BYTES = 1000000UL;

constexpr uint8_t PIN_OLED_PWR = 36, PIN_OLED_RST = 21, PIN_OLED_SDA = 17, PIN_OLED_SCL = 18;
U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(U8G2_R0, PIN_OLED_SCL, PIN_OLED_SDA, PIN_OLED_RST);
SX1262 radio = new Module(8, 14, 12, 13);
WebServer web(80);

const char *CAND_NOME[5] = {"PARADO", "LOCOMOCAO", "CABECA_BAIXA", "RITMO_MASTIGACAO", "OUTRO_ATIVO"};
const char *CAB_BASE = "ms_base,animal,boot_id,seq,tentativa,tipo,pacotes_perdidos_antes,rssi,snr,bat_mv,temp_chip_c,candidato_agora,"
                       "intervalo_s,seg_parado,seg_locomocao,seg_cabeca_baixa,seg_ritmo,seg_outro,passos_intervalo,passos_hora,indice,flags,"
                       "uptime_colar_s,passos_total,tot_parado,tot_locomocao,tot_cabeca_baixa,tot_ritmo,tot_outro,crc";
constexpr int N_CAMPOS = 30;

struct Ref { bool valida, doCsv; uint32_t boot_id; uint16_t seq; uint32_t uptime_s, passos_total, seg_total[5]; };
struct Colar { bool usado; char id[11]; Ref ref; PacoteLoRa ult; bool temUlt; float rssi, snr; uint32_t vistoMs, pacotes, perdidos, duplicados, reinicios, reconstruidos, semAck; uint32_t pctUlt[5]; };
Colar colares[MAX_COLARES];
volatile bool chegou = false; bool fsOk = false;
uint32_t totalPacotes = 0, invalidos = 0, acks = 0, rotacoes = 0, logFalhas = 0, linhasLidas = 0, linhasRuins = 0;
// API: isrRx — Sinaliza interrupção DIO1 do SX1262 sem processar rádio dentro da ISR.
void IRAM_ATTR isrRx() { chegou = true; }

// API: fnv — Calcula checksum FNV-1a da linha CSV; não é criptografia nem autenticação.
uint32_t fnv(const char *s, size_t n) { uint32_t h = 2166136261UL; for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619UL; } return h; }
Colar *acharColar(const char *id) {
  for (auto &c : colares) if (c.usado && strncmp(c.id, id, 10) == 0) return &c;
  for (auto &c : colares) if (!c.usado) { memset(&c, 0, sizeof(c)); c.usado = true; strncpy(c.id, id, 10); return &c; }
  return nullptr;
}

// ---------- reconstrucao das referencias a partir do CSV ----------
// API: aplicarLinha — Valida CRC e campos de uma linha persistida e restaura a última referência do colar.
void aplicarLinha(char *linha) {
  size_t n = strlen(linha); while (n && (linha[n - 1] == '\r' || linha[n - 1] == '\n')) linha[--n] = 0;
  char *ult = strrchr(linha, ','); if (!ult || strlen(ult + 1) != 8) { linhasRuins++; return; }
  if (strtoul(ult + 1, nullptr, 16) != fnv(linha, ult - linha)) { linhasRuins++; return; }   // crc da linha
  char *campo[N_CAMPOS]; int k = 0; campo[k++] = linha;
  for (char *p = linha; *p && k < N_CAMPOS; p++) if (*p == ',') { *p = 0; campo[k++] = p + 1; }  // preserva campos vazios
  if (k != N_CAMPOS) { linhasRuins++; return; }
  Colar *c = acharColar(campo[1]); if (!c) return;
  Ref &r = c->ref; r.valida = true; r.doCsv = true;
  r.boot_id = strtoul(campo[2], nullptr, 16); r.seq = (uint16_t)strtoul(campo[3], nullptr, 10);
  r.uptime_s = strtoul(campo[22], nullptr, 10); r.passos_total = strtoul(campo[23], nullptr, 10);
  for (int i = 0; i < 5; i++) r.seg_total[i] = strtoul(campo[24 + i], nullptr, 10);
  linhasLidas++;
}
// API: lerArquivoRefs — Percorre CSV da base e recupera referências, ignorando linhas incompletas ou inválidas.
void lerArquivoRefs(const char *path) {
  if (!LittleFS.exists(path)) return;
  File f = LittleFS.open(path, FILE_READ); if (!f) return;
  static char buf[400]; f.readStringUntil('\n');   // cabecalho
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', buf, sizeof(buf) - 1); buf[n] = 0;
    if (n && strncmp(buf, "ms_base", 7) != 0) aplicarLinha(buf);
  }
  f.close();
}
// API: prepararArquivos — Migra formato antigo e prepara arquivo atual, recuperando o estado após reinício.
void prepararArquivos() {
  // formato antigo (v1.2/v1.3) -> guarda e comeca novo
  if (LittleFS.exists("/base.csv")) {
    File f = LittleFS.open("/base.csv", FILE_READ); String h = f ? f.readStringUntil('\n') : String(""); if (f) f.close(); h.trim();
    if (h != CAB_BASE) { LittleFS.remove("/base_v13.csv"); LittleFS.rename("/base.csv", "/base_v13.csv"); }
  }
  if (!LittleFS.exists("/base.csv")) { File f = LittleFS.open("/base.csv", FILE_WRITE); if (f) { f.println(CAB_BASE); f.close(); } }
  // ultima linha incompleta (corte durante escrita): fecha com '\n' para a proxima nao grudar nela
  File f = LittleFS.open("/base.csv", FILE_READ);
  if (f && f.size() > 0) { f.seek(f.size() - 1); int u = f.read(); f.close(); if (u != '\n') { File a = LittleFS.open("/base.csv", FILE_APPEND); if (a) { a.print("\n"); a.close(); } } }
  else if (f) f.close();
  LittleFS.remove("/refs_a.bin"); LittleFS.remove("/refs_b.bin");   // v1.3 (nao usados mais)
  lerArquivoRefs("/base_antigo.csv"); lerArquivoRefs("/base.csv");
  Serial.printf("Referencias do CSV: %lu linhas validas, %lu ignoradas\n", (unsigned long)linhasLidas, (unsigned long)linhasRuins);
}
// true = linha inteira confirmada na flash
// API: gravarLog — Grava linha de telemetria com rotação de CSV e confere número de bytes escritos.
bool gravarLog(const char *l) {
  if (!fsOk) return false;
  File f = LittleFS.open("/base.csv", FILE_APPEND); if (!f) return false;
  if (f.size() > LOG_MAX_BYTES) {   // rotacao: base_antigo.csv continua sendo lido no boot
    f.close(); LittleFS.remove("/base_antigo.csv"); LittleFS.rename("/base.csv", "/base_antigo.csv"); rotacoes++;
    f = LittleFS.open("/base.csv", FILE_WRITE); if (!f) return false; f.println(CAB_BASE);
  }
  size_t n = f.print(l); f.close();
  return n == strlen(l);
}

// ---------- OLED ----------
// API: oled — Mostra resumo local da base, qualidade do enlace e estado do armazenamento.
void oled(const Colar *c) {
  tela.clearBuffer(); tela.setFont(u8g2_font_6x10_tf); char l[32];
  snprintf(l, sizeof(l), "BASE pac %lu ack %lu", (unsigned long)totalPacotes, (unsigned long)acks); tela.drawStr(0, 9, l); tela.drawHLine(0, 12, 128);
  if (c) {
    tela.setFont(u8g2_font_helvB12_tr); tela.drawStr(0, 30, c->id); tela.setFont(u8g2_font_6x10_tf);
    tela.drawStr(0, 42, CAND_NOME[c->ult.candidato < 5 ? c->ult.candidato : 0]);
    snprintf(l, sizeof(l), "RSSI %.0f SNR %.1f", c->rssi, c->snr); tela.drawStr(0, 53, l);
    snprintf(l, sizeof(l), "BAT %.2fV %s", c->ult.bat_mv / 1000.0f, fsOk ? (logFalhas ? "FLASH ERR" : "") : "SEM FLASH"); tela.drawStr(0, 64, l);
  } else tela.drawStr(0, 34, fsOk ? "Aguardando colares..." : "SEM FLASH!");
  tela.sendBuffer();
}

// ---------- recepcao ----------
// API: enviarAck — Responde ACK v5 após tratamento do pacote e atraso de rádio configurado.
void enviarAck(const PacoteLoRa &p, float rssi, uint32_t t0) {
  AckLoRa a; memset(&a, 0, sizeof(a)); a.magic = 0xC1; a.versao = SC_LORA_VERSAO; memcpy(a.animal, p.animal, 10); a.seq = p.seq;
  a.rssi_base = (int8_t)constrain((int)lroundf(rssi), -128, 127);
  while (millis() - t0 < ACK_ATRASO_MS) delay(1);
  if (radio.transmit((const uint8_t *)&a, sizeof(a)) == RADIOLIB_ERR_NONE) acks++;
}
// API: voltarRx — Rearma SX1262 em modo recepção após TX ou erro.
void voltarRx() { chegou = false; radio.startReceive(); }   // DIO1 do proprio ACK nao e recepcao
// API: receber — Valida pacote, reconstrói deltas por boot ID e persiste CSV antes de confirmar.
void receber() {
  const uint32_t t0 = millis();
  PacoteLoRa p; memset(&p, 0, sizeof(p)); size_t n = radio.getPacketLength();
  int rc = radio.readData((uint8_t *)&p, sizeof(p));
  float rssi = radio.getRSSI(), snr = radio.getSNR();
  if (rc != RADIOLIB_ERR_NONE || n != sizeof(p) || p.magic != 0xC0 || p.versao != SC_LORA_VERSAO) { invalidos++; voltarRx(); return; }
  char id[11]; memcpy(id, p.animal, 10); id[10] = 0;
  Colar *c = acharColar(id); if (!c) { voltarRx(); return; }
  Ref &r = c->ref;
  const bool mesmoBoot = r.valida && p.boot_id == r.boot_id;
  // Duplicata: este seq ja esta GRAVADO (a referencia so avanca depois da gravacao) -> confirma de novo
  if (mesmoBoot && (p.seq == r.seq || (uint16_t)(p.seq - r.seq) > 30000)) { c->duplicados++; enviarAck(p, rssi, t0); voltarRx(); return; }
  // Tipos: INTERVALO | INTERVALO_APOS_REINICIO_BASE (referencia lida do CSV no boot) | REINICIO_COLAR | BASELINE
  const char *tipo; uint32_t dSeg[5] = {0, 0, 0, 0, 0}, dPassos = 0, dT = 0; uint16_t perdidosAqui = 0; bool valido = true;
  if (mesmoBoot) {
    tipo = r.doCsv ? "INTERVALO_APOS_REINICIO_BASE" : "INTERVALO";
    for (int i = 0; i < 5; i++) dSeg[i] = p.seg_total[i] - r.seg_total[i];
    dPassos = p.passos_total - r.passos_total; dT = p.uptime_s - r.uptime_s; perdidosAqui = (uint16_t)(p.seq - r.seq) - 1;
  } else if (r.valida) {
    tipo = "REINICIO_COLAR"; for (int i = 0; i < 5; i++) dSeg[i] = p.seg_total[i];
    dPassos = p.passos_total; dT = p.uptime_s;
  } else { tipo = "BASELINE"; valido = false; }
  char iv[96], l[400];
  if (valido) snprintf(iv, sizeof(iv), "%lu,%lu,%lu,%lu,%lu,%lu,%lu", (unsigned long)dT, (unsigned long)dSeg[0], (unsigned long)dSeg[1], (unsigned long)dSeg[2], (unsigned long)dSeg[3], (unsigned long)dSeg[4], (unsigned long)dPassos);
  else strcpy(iv, ",,,,,,");
  int m = snprintf(l, sizeof(l) - 12, "%lu,%.10s,%08lX,%u,%u,%s,%u,%.0f,%.1f,%u,%d,%s,%s,%u,%.1f,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
                   (unsigned long)millis(), id, (unsigned long)p.boot_id, p.seq, p.tentativa, tipo, perdidosAqui, rssi, snr, p.bat_mv, p.temp_c,
                   CAND_NOME[p.candidato < 5 ? p.candidato : 0], iv, p.passos_hora, p.indice_x10 == 255 ? -1.0f : p.indice_x10 / 10.0f, p.flags,
                   (unsigned long)p.uptime_s, (unsigned long)p.passos_total, (unsigned long)p.seg_total[0], (unsigned long)p.seg_total[1],
                   (unsigned long)p.seg_total[2], (unsigned long)p.seg_total[3], (unsigned long)p.seg_total[4]);
  if (m < 0 || m >= (int)sizeof(l) - 12) { invalidos++; voltarRx(); return; }
  snprintf(l + m, 12, ",%08lX\n", (unsigned long)fnv(l, m));
  Serial.print(l);
  // 1) grava  2) avanca referencia  3) ACK. Sem flash: so Serial (painel avisa).
  const bool gravado = fsOk ? gravarLog(l) : true;
  if (!gravado) { logFalhas++; c->semAck++; voltarRx(); oled(c); return; }   // sem ACK: colar retransmite
  if (mesmoBoot) { c->perdidos += perdidosAqui; if (perdidosAqui) c->reconstruidos++; } else if (r.valida) c->reinicios++;
  r.valida = true; r.doCsv = false; r.boot_id = p.boot_id; r.seq = p.seq; r.uptime_s = p.uptime_s; r.passos_total = p.passos_total;
  for (int i = 0; i < 5; i++) r.seg_total[i] = p.seg_total[i];
  uint32_t tot = 0; for (int i = 0; i < 5; i++) tot += dSeg[i];
  for (int i = 0; i < 5; i++) c->pctUlt[i] = tot ? 100UL * dSeg[i] / tot : 0;
  c->ult = p; c->temUlt = true; c->rssi = rssi; c->snr = snr; c->vistoMs = millis(); c->pacotes++; totalPacotes++;
  enviarAck(p, rssi, t0); voltarRx(); oled(c);
}

// ---------- painel ----------
// API: pagina — Constrói a página HTML local com estados e diagnósticos dos colares.
void pagina() {
  String h = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><meta http-equiv=refresh content=10>"
               "<title>Base</title><style>body{font:14px system-ui;background:#091521;color:#e6f3f8;margin:16px}table{border-collapse:collapse;width:100%}"
               "td,th{border-bottom:1px solid #2b485d;padding:6px;text-align:left}.a{color:#fca5a5;font-weight:700}a{color:#5eead4}</style>"
               "<h2>Smart Collar · Estação-base v1.4</h2><table><tr><th>Animal</th><th>Visto há</th><th>RSSI/SNR</th><th>Candidato</th><th>% parado/loc/cab.baixa/ritmo/outro</th><th>Índice</th><th>Bateria</th><th>Perdidos/reconstruídos/dup./reinícios/sem ACK</th></tr>");
  for (auto &c : colares) {
    if (!c.usado) continue; char l[640]; char ind[8];
    if (!c.temUlt) { snprintf(l, sizeof(l), "<tr><td>%.10s</td><td colspan=7>referência do CSV (boot %08lX, seq %u) · aguardando pacote</td></tr>", c.id, (unsigned long)c.ref.boot_id, c.ref.seq); h += l; continue; }
    if (c.ult.indice_x10 == 255) strcpy(ind, "-"); else snprintf(ind, sizeof(ind), "%.1f", c.ult.indice_x10 / 10.0f);
    snprintf(l, sizeof(l), "<tr><td>%.10s</td><td>%lus</td><td>%.0f / %.1f</td><td>%s</td><td>%u/%u/%u/%u/%u</td><td class='%s'>%s</td><td>%.2f V</td><td>%lu/%lu/%lu/%lu/%lu</td></tr>",
             c.id, (unsigned long)((millis() - c.vistoMs) / 1000), c.rssi, c.snr, CAND_NOME[c.ult.candidato < 5 ? c.ult.candidato : 0],
             (unsigned)c.pctUlt[0], (unsigned)c.pctUlt[1], (unsigned)c.pctUlt[2], (unsigned)c.pctUlt[3], (unsigned)c.pctUlt[4], (c.ult.flags & 1) ? "a" : "", ind,
             c.ult.bat_mv / 1000.0f, (unsigned long)c.perdidos, (unsigned long)c.reconstruidos, (unsigned long)c.duplicados, (unsigned long)c.reinicios, (unsigned long)c.semAck);
    h += l;
  }
  size_t livre = fsOk ? LittleFS.totalBytes() - LittleFS.usedBytes() : 0;
  h += "</table><p><a href=/base.csv>base.csv</a> · <a href=/base_antigo.csv>base_antigo.csv</a> · inválidos: " + String(invalidos) + " · ACKs: " + String(acks) + "</p>";
  h += "<p>Flash livre: " + String(livre / 1024) + " kB · rotações: " + String(rotacoes) + " · falhas de gravação: " + String(logFalhas) +
       " · referências do CSV no boot: " + String(linhasLidas) + " linhas (" + String(linhasRuins) + " ignoradas)</p>";
  if (!fsOk) h += "<p class=a>LittleFS INDISPONÍVEL: dados só na Serial; nada sobrevive a desligamento.</p>";
  else if (livre < 150000) h += "<p class=a>Pouco espaço na flash: baixe os CSV.</p>";
  if (logFalhas) h += "<p class=a>Houve falhas de gravação: esses pacotes NÃO foram confirmados (o colar retransmite).</p>";
  web.send(200, "text/html; charset=utf-8", h);
}
// API: enviarCsv — Disponibiliza download do arquivo CSV selecionado via painel.
void enviarCsv(const char *path) {
  if (!fsOk || !LittleFS.exists(path)) { web.send(404, "text/plain", "arquivo inexistente"); return; }
  File f = LittleFS.open(path, FILE_READ); web.streamFile(f, "text/csv"); f.close();
}
// API: setup — Inicializa display, armazenamento, recupera referências e ativa LoRa.
void setup() {
  Serial.begin(115200); delay(300); Serial.println("\nSMART COLLAR BASE v1.4");
  pinMode(PIN_OLED_PWR, OUTPUT); digitalWrite(PIN_OLED_PWR, LOW); delay(200); tela.begin(); oled(nullptr);
  fsOk = LittleFS.begin(false);
  if (fsOk) prepararArquivos(); else Serial.println("LittleFS indisponivel: so Serial");
  SPI.begin(9, 11, 10, 8);
  int rc = loraBeginHeltec(radio, LORA_FREQ_MHZ, LORA_POTENCIA_DBM);
  Serial.printf("LoRa: %d\n", rc);
  radio.setDio1Action(isrRx); radio.startReceive();
  WiFi.mode(WIFI_AP); WiFi.softAPConfig(IPAddress(192, 168, 5, 1), IPAddress(192, 168, 5, 1), IPAddress(255, 255, 255, 0)); WiFi.softAP(AP_NOME, AP_SENHA);
  web.on("/", pagina);
  web.on("/base.csv", [] { enviarCsv("/base.csv"); });
  web.on("/base_antigo.csv", [] { enviarCsv("/base_antigo.csv"); });
  web.begin(); Serial.println("Base: http://192.168.5.1"); oled(nullptr);
}
// API: loop — Atende HTTP e pacotes LoRa, mantendo execução sem bloqueios prolongados.
void loop() {
  if (chegou) { chegou = false; receber(); }
  web.handleClient(); delay(2);
}
