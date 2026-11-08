/*
  CHIHUAHUA LAB v1.5 - TESTE SUPERVISIONADO
  Heltec WiFi LoRa 32 V3.2 + MPU6050 + OLED + dashboard Wi-Fi
  Baseado na v0.5 que funcionou no hardware do projeto.

  CORRECOES v1.5 (sobre a v1.4):
   - ERRO DE COMPILACAO corrigido: "else" sem "if" em enviarStatus() (v1.4 nao compilava).
   - MPU6050 com filtro DLPF ~21 Hz, faixas +-4 g / +-500 graus/s (cachorro correndo saturava +-2 g).
   - Calibracao recusada se o sensor estiver se mexendo (antes gravava bias errado).
   - Amostragem em ritmo fixo (proximaLeitura += 50 ms) em vez de acumular atraso.
   - Contagem de amostras atrasadas no diagnostico.
   - LoRa (se habilitado) nao bloqueante: transmit() travava o loop ~0,2 s a cada envio.
   - LoRa compativel com RadioLib 7.x e 8.x (a 8.0 mudou o begin()).

  LIGACOES JA EXISTENTES (NAO ALTERAR):
    MPU VCC -> Heltec 3V3
    MPU GND -> Heltec GND
    MPU SDA -> Heltec GPIO4
    MPU SCL -> Heltec GPIO5

  Bibliotecas: U8g2 by oliver + bibliotecas incluidas no Heltec ESP32.
  Serial Monitor: 115200 baud. Heltec WiFi LoRa 32 (V3).

  LoRa desligado. A leitura VBAT mede tensao, nao detecta carregamento.
  Use somente bateria de 1 celula Li-ion/LiPo verificada e com protecao apropriada.
  O Wi-Fi AP e local, sem acesso a internet. Troque a senha abaixo.
  Historico curto em RAM e historico estendido em LittleFS (quando disponivel).
  Classificacao experimental: NAO diagnostica comportamento animal.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <math.h>

// ====== CONFIGURACAO DO USUARIO ======
#include "Chihuahua_config_local.h"  // Copie .example.h; nunca commite credenciais.

// GPIO37 habilita leitura via divisor para GPIO1, conforme exemplo Heltec V3.2.
// Habilitado para permitir leitura quando houver bateria COMPATIVEL.
// NAO confirma quimica, protecao, polaridade, seguranca de carga ou estado de recarga.
constexpr bool MEDIR_BATERIA = true;

// LoRa TX opcional para FUTURA estacao-base. Exige RadioLib e 2a placa.
// Nao habilitar sem antena conectada, variante 863-928 MHz e requisitos RF locais.
#define HABILITAR_LORA_TX 0
#if HABILITAR_LORA_TX
  #include <SPI.h>
  #include <RadioLib.h>
  SX1262 radio = new Module(8, 14, 12, 13); // CS, DIO1, RST, BUSY Heltec V3
  bool loraPronto = false, loraOcupado = false;
  volatile bool loraFim = false;
  uint32_t ultimaTx = 0;
// API: loraIsr — Sinaliza fim de transmissão por interrupção, sem executar processamento na ISR.
  void IRAM_ATTR loraIsr() { loraFim = true; }
#endif

// ====== PINAGEM QUE FOI TESTADA NA v0.5 ======
constexpr uint8_t MPU_SDA = 4;
constexpr uint8_t MPU_SCL = 5;
constexpr uint8_t OLED_PWR = 36; // Vext LOW=on
constexpr uint8_t OLED_RST = 21;
constexpr uint8_t OLED_SDA = 17;
constexpr uint8_t OLED_SCL = 18;
constexpr uint8_t ADC_CTRL = 37;
constexpr uint8_t ADC_VBAT = 1;
constexpr uint32_t INTERVALO_AMOSTRA_MS = 50; // 20 Hz
constexpr int AMOSTRAS_POR_JANELA = 20;        // 1 leitura agregada por segundo
constexpr int CAPACIDADE_HISTORICO = 900;      // ate ~15 minutos em RAM
constexpr uint32_t INTERVALO_LOG_FLASH_MS = 10000; // 1 linha a cada 10 s
constexpr size_t LIMITE_LOG_FLASH_BYTES = 512 * 1024; // previne crescimento infinito
const char *ARQUIVO_LOG_FLASH = "/chihuahua_resumo.csv";
const char *ARQUIVO_MARCACOES = "/chihuahua_marcacoes.csv";
const char *ROTULOS[] = {"SEM_ROTULO","DESCANSANDO","CAMINHANDO","CORRENDO","BRINCANDO","COMENDO","BEBENDO","OUTRO"};
uint8_t rotuloHumano = 0;
bool gravacaoAtiva = false;
uint32_t tempoEstados[3] = {0,0,0};
void registrarMarcacao(const char *tipo);
void baixarMarcacoes();

U8G2_SSD1306_128X64_NONAME_F_SW_I2C tela(
    U8G2_R0, /* clock */ OLED_SCL, /* data */ OLED_SDA,
    /* reset */ OLED_RST);
TwoWire scannerOLED(1);
WebServer servidor(80);

// Estados e historico (sem gravacao constante na memoria flash)
enum Estado : uint8_t { PARADO = 0, LEVE = 1, INTENSO = 2, CALIBRANDO = 3, ERRO_MPU = 4 };
const char *nomeEstado(Estado estado) {
  switch (estado) {
    case PARADO: return "PARADO";
    case LEVE: return "LEVE";
    case INTENSO: return "INTENSO";
    case CALIBRANDO: return "CALIBRANDO";
    default: return "ERRO MPU";
  }
}
// Prototipos explicitos para evitar problema com tipos personalizados no Arduino IDE.
const char *nomeEstado(Estado estado);
void adicionarHistorico(float acc, float giro, Estado estado);
void enviarStatus();
void enviarHistorico();
void baixarCSV();

struct Medicao {
  uint32_t tempoSegundos;
  float acc;
  float giro;
  uint8_t estado;
};
Medicao historico[CAPACIDADE_HISTORICO];
uint16_t indiceHistorico = 0, totalHistorico = 0;
uint32_t sequencia = 0;

uint8_t enderecoMPU = 0;
float biasGx = 0, biasGy = 0, biasGz = 0;
float gravX = 0, gravY = 0, gravZ = 0;
bool primeiroDado = true;
float somaAcc2 = 0, somaGiro2 = 0;
int contagem = 0;
uint32_t proximaLeitura = 0;
uint32_t ultimaTentativa = 0;
float accAtual = 0.0f, giroAtual = 0.0f;
float bateriaVolts = -1.0f;
Estado estadoAtual = CALIBRANDO;
bool solicitarCalibracao = false;
bool flashPronta = false;
bool flashCheia = false;
uint32_t ultimoLogFlash = 0;
uint32_t idSessao = 0;
uint32_t linhasLogFlash = 0;
void iniciarLogFlash();
void registrarLogFlash(float acc, float giro, Estado estado);
void baixarLogFlash();
uint32_t ultimoADC = 0;
uint32_t amostrasAtrasadas = 0;
bool ultimaCalibOk = false;
// v1.5: +-4 g e +-500 graus/s (antes +-2 g / +-250, saturava em corrida/brincadeira)
constexpr float ACC_LSB = 8192.0f;
constexpr float GYR_LSB = 65.5f;
// Estimativa aproximada para UMA celula Li-ion/LiPo 3,7 V nominal.
// A tensao sob carga e durante recarga NAO corresponde a percentual preciso.
// Percentual por curva fixa de tensao removido: nao representa carga real.



const char PAGINA[] PROGMEM = R"SC_HTML(
<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Chihuahua Lab · Monitor de Atividade</title>
<style>:root{color-scheme:dark;--bg:#0b1423;--card:#14243a;--line:#304661;--ink:#eef7ff;--muted:#a3b6ca;--primary:#a5f3fc;--pink:#f9a8d4}
*{box-sizing:border-box}body{background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,Segoe UI,sans-serif;margin:0}
main{max-width:980px;margin:0 auto;padding:22px 15px 38px}.header{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;margin-bottom:18px}h1{font-size:26px;margin:0}h2{font-size:16px;margin:0 0 12px}small,.muted{color:var(--muted)}.tag{border:1px solid var(--line);border-radius:24px;padding:7px 12px;font-size:12px}
.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:11px}.card{background:var(--card);border:1px solid var(--line);border-radius:15px;padding:17px;margin-bottom:12px}.label{color:var(--muted);font-size:12px}.value{font-size:27px;font-weight:750;margin-top:5px}.value.hot{color:var(--pink)}.value.cool{color:var(--primary)}.buttons{display:flex;gap:9px;flex-wrap:wrap;margin-top:12px}button,a.btn,select{background:#253e59;border:1px solid #56758e;color:var(--ink);font:inherit;padding:9px 12px;border-radius:9px;cursor:pointer;text-decoration:none}button:disabled{opacity:.45}canvas{height:200px;width:100%;display:block}.note{font-size:12px;color:var(--muted);margin-top:9px}
@media(max-width:640px){.grid{grid-template-columns:1fr 1fr}.grid .card:first-child{grid-column:span 2}.value{font-size:23px}}
</style></head><body><main><header class="header"><div><h1>🐶 Chihuahua Lab</h1><small>Teste de atividade · LAB-001 · dados locais · sem GPS</small></div><b class="tag" id="online">Conectando...</b></header>
<section class="grid"><div class="card"><div class="label">Movimento atual</div><div class="value cool" id="state">–</div></div><div class="card"><div class="label">Aceleração dinâmica</div><div class="value"><span id="acc">–</span> <small>g RMS</small></div></div><div class="card"><div class="label">Rotação</div><div class="value"><span id="giro">–</span> <small>°/s</small></div></div><div class="card"><div class="label">Bateria · tensao</div><div class="value" id="battery">—</div><div class="note" id="batteryV">Aguardando leitura...</div></div></section>
<section class="card"><h2>Atividade recente</h2><canvas id="graph"></canvas><div class="note">Movimento é estimado pelo MPU6050. PARADO não significa necessariamente dormindo; LEVE e INTENSO não distinguem caminhada, brincadeira ou corrida.</div></section>
<section class="card"><h2>Tempo por intensidade desde que ligou</h2><div id="times">–</div></section>
<section class="card"><h2>Registrar observações para futura IA</h2><label for="label">O que você está observando agora?</label><div class="buttons"><select id="label"><option value="0">SEM RÓTULO</option><option value="1">DESCANSANDO</option><option value="2">CAMINHANDO</option><option value="3">CORRENDO</option><option value="4">BRINCANDO</option><option value="5">COMENDO</option><option value="6">BEBENDO</option><option value="7">OUTRO</option></select><button id="mark">Marcar observação</button></div><p class="note">O rótulo é inserido por você, não pelo algoritmo. Use vídeos com horários anotados para validar os dados depois.</p></section>
<section class="card"><h2>Gravação da tarde</h2><div id="rec">Gravação: verificando...</div><div class="buttons"><button id="start">Iniciar sessão</button><button id="stop">Encerrar sessão</button><button id="recal">Recalibrar parada</button></div><div class="buttons"><a class="btn" href="/download-full.csv">CSV da tarde</a><a class="btn" href="/download-labels.csv">CSV das observações</a><a class="btn" href="/download.csv">CSV dos 15 min recentes</a></div><p class="note">O CSV longo é gravado a cada 10 segundos na flash somente se LittleFS estiver disponível e a sessão tiver sido iniciada. Baixe os arquivos após o teste. A flash tem capacidade limitada.</p></section>
<section class="card"><h2>Diagnóstico</h2><div id="diag">–</div><p class="note">Bateria: leitura de tensao, nao porcentagem real. USB e consumo alteram leitura. Nao detecta carregamento. Sem medicao de GPS, temperatura corporal, sono ou saude. O Wi-Fi funciona localmente.</p></section>
<footer class="note">Experimento de bancada/peitoral, sempre supervisionado. Nunca deixe placas, fios ou bateria expostos ao animal.</footer></main><script>
const $=id=>document.getElementById(id);let data=[],lastSeq=-1,offline=0;
function plot(){const c=$('graph'),r=c.getBoundingClientRect(),scale=window.devicePixelRatio||1;let w=r.width,h=200,p=34;c.width=Math.max(1,Math.round(w*scale));c.height=Math.round(h*scale);let x=c.getContext('2d');x.setTransform(scale,0,0,scale,0,0);x.clearRect(0,0,w,h);x.strokeStyle='#344e68';for(let i=0;i<5;i++){const y=12+i*42;x.beginPath();x.moveTo(p,y);x.lineTo(w-8,y);x.stroke();}const max=Math.max(.12,...data.map(v=>v.a))*1.1;x.fillStyle='#b9cbdb';x.font='11px system-ui';x.fillText(max.toFixed(2)+'g',0,12);x.fillText('0',17,181);if(data.length<2)return;x.strokeStyle='#a5f3fc';x.lineWidth=2.5;x.beginPath();for(let i=0;i<data.length;i++){let xx=p+(w-p-8)*i/(data.length-1),yy=12+168*(1-data[i].a/max);i?x.lineTo(xx,yy):x.moveTo(xx,yy)}x.stroke()}
async function action(path,params){let r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params||''});let t=await r.text();if(!r.ok)throw Error(t);alert(t)}
async function status(){try{let r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error('offline');let v=await r.json();offline=0;$('online').textContent='Online';$('state').textContent=v.estado;$('state').className='value '+(v.estado==='INTENSO'?'hot':'cool');$('acc').textContent=v.acc.toFixed(3);$('giro').textContent=v.giro.toFixed(1);$('battery').textContent=v.bat_v===null?'Sem leitura':v.bat_v.toFixed(2)+' V';$('batteryV').textContent='Tensao filtrada (nao e % de carga)'+(v.bat_low?' · Verificar bateria baixa':'')+' · USB pode alterar leitura';$('times').textContent='Parado: '+v.parado_s+' s · Leve: '+v.leve_s+' s · Intenso: '+v.intenso_s+' s';$('diag').textContent='MPU6050: '+(v.sensor?'OK':'não encontrado')+' · Tempo ligado: '+Math.floor(v.uptime_s/60)+' min · Flash: '+(v.flash_ok?(v.flash_full?'limite atingido':'OK'):'indisponível')+' · Amostras atrasadas: '+v.atrasadas+' · Calibração: '+(v.calib_ok?'OK':'não aplicada (mexeu?)');$('rec').textContent='Gravação: '+(v.gravacao?'ATIVA':'parada')+' · Rótulo: '+v.rotulo;if(lastSeq!==v.seq){lastSeq=v.seq;if(v.seq){data.push({a:v.acc});data=data.slice(-180);plot()}}}catch(e){if(++offline>=2)$('online').textContent='Sem conexão'}}
async function initialize(){try{let r=await fetch('/api/history',{cache:'no-store'});if(!r.ok)throw Error();let h=await r.json();data=h.map(x=>({a:x.a})).slice(-180);plot()}catch(e){}}
function bind(id,fn){$(id).addEventListener('click',async()=>{try{await fn();await status()}catch(e){alert('Falha: '+e.message)}})}
bind('start',()=>action('/api/record/start'));bind('stop',()=>action('/api/record/stop'));bind('mark',()=>action('/api/label','code='+encodeURIComponent($('label').value)));bind('recal',()=>action('/api/calibrate'));
window.addEventListener('resize',plot);initialize().then(status);setInterval(status,1000);
</script></body></html>
)SC_HTML";

// ====== SENSOR: MESMAS FORMULAS DA v0.5 ======
// API: sensorResponde — Verifica endereço I2C do GY-521 / MPU6050.
bool sensorResponde(uint8_t endereco) {
  Wire.beginTransmission(endereco);
  return Wire.endTransmission() == 0;
}
// API: escreverRegistrador — Escreve configuração MPU6050 e informa erro I2C.
bool escreverRegistrador(uint8_t reg, uint8_t valor) {
  Wire.beginTransmission(enderecoMPU);
  Wire.write(reg);
  Wire.write(valor);
  return Wire.endTransmission() == 0;
}
// API: iniciarSensor — Configura faixas e filtro do MPU6050 para teste supervisionado com o cão.
bool iniciarSensor() {
  if (sensorResponde(0x68)) enderecoMPU = 0x68;
  else if (sensorResponde(0x69)) enderecoMPU = 0x69;
  else return false;
  bool ok = escreverRegistrador(0x6B, 0x01) &&   // acorda, relogio PLL do giro X
            escreverRegistrador(0x1A, 0x04) &&   // DLPF ~21 Hz (reduz ruido/aliasing a 20 Hz)
            escreverRegistrador(0x1C, 0x08) &&   // acel +-4 g
            escreverRegistrador(0x1B, 0x08);     // giro +-500 graus/s
  if (!ok) enderecoMPU = 0;
  return ok;
}
bool lerSensor(float &ax, float &ay, float &az,
               float &gx, float &gy, float &gz) {
  Wire.beginTransmission(enderecoMPU);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(enderecoMPU, (uint8_t)14) != 14) return false;
  int16_t raX = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t raY = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t raZ = (int16_t)((Wire.read() << 8) | Wire.read());
  Wire.read(); Wire.read(); // ignora temperatura interna
  int16_t rgX = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t rgY = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t rgZ = (int16_t)((Wire.read() << 8) | Wire.read());
  ax = raX / ACC_LSB; ay = raY / ACC_LSB; az = raZ / ACC_LSB;
  gx = rgX / GYR_LSB; gy = rgY / GYR_LSB; gz = rgZ / GYR_LSB;
  return true;
}
// API: mostrarTela — Desenha estado de atividade no OLED da Heltec.
void mostrarTela(const char *estado, float acc, float giro, bool medidas) {
  tela.clearBuffer();
  tela.setFont(u8g2_font_6x10_tf);
  tela.drawStr(0, 9, "CHIHUAHUA v1.5");
  tela.drawHLine(0, 12, 128);
  tela.setFont(u8g2_font_helvB14_tr);
  tela.drawStr(0, 34, estado);
  tela.setFont(u8g2_font_6x10_tf);
  char linha[32];
  if (medidas) {
    snprintf(linha, sizeof(linha), "ACC %.3f g", acc); tela.drawStr(0, 43, linha);
    snprintf(linha, sizeof(linha), "GIRO %.1f d/s", giro); tela.drawStr(0, 53, linha);
    if (bateriaVolts > 0.0f) snprintf(linha, sizeof(linha), "BAT %.2fV", bateriaVolts);
    else snprintf(linha, sizeof(linha), "BAT sem leitura");
    tela.drawStr(0, 63, linha);
  } else {
    tela.drawStr(0, 52, "MPU6050 + WiFi");
  }
  tela.sendBuffer();
}
// API: iniciarOLED — Inicializa display onboard e seus pinos de alimentação.
void iniciarOLED() {
  // COPIA do diagnostico U8g2 e da v0.5 que funcionaram.
  pinMode(OLED_PWR, OUTPUT);
  digitalWrite(OLED_PWR, LOW);
  delay(300);
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(30);
  digitalWrite(OLED_RST, HIGH);
  delay(120);
  scannerOLED.begin(OLED_SDA, OLED_SCL, 100000);
  scannerOLED.beginTransmission(0x3C);
  int err3c = scannerOLED.endTransmission();
  scannerOLED.beginTransmission(0x3D);
  int err3d = scannerOLED.endTransmission();
  scannerOLED.end();
  if (err3c && !err3d) tela.setI2CAddress(0x3D * 2);
  tela.begin(); tela.setContrast(255);
  mostrarTela("INICIANDO", 0, 0, false);
  Serial.printf("OLED 0x3C: %s / 0x3D: %s\n", err3c == 0 ? "OK" : "NAO", err3d == 0 ? "OK" : "NAO");
}
// API: calibrarParado — Calcula referência de ruído e movimento basal com dispositivo imóvel.
void calibrarParado() {
  estadoAtual = CALIBRANDO;
  mostrarTela("CALIBRANDO", 0, 0, false);
  Serial.println("CALIBRACAO: deixe o sensor PARADO por 3 segundos...");
  delay(1000);
  float somaX = 0, somaY = 0, somaZ = 0, s2X = 0, s2Y = 0, s2Z = 0, aMin = 99, aMax = 0;
  float ax, ay, az, gx, gy, gz;
  int validas = 0;
  for (int i = 0; i < 50; ++i) {
    if (lerSensor(ax, ay, az, gx, gy, gz)) {
      somaX += gx; somaY += gy; somaZ += gz; validas++;
      s2X += gx * gx; s2Y += gy * gy; s2Z += gz * gz;
      float am = sqrtf(ax * ax + ay * ay + az * az); aMin = fminf(aMin, am); aMax = fmaxf(aMax, am);
    }
    delay(40);
  }
  ultimaCalibOk = false;
  if (validas > 10) {
    float mx = somaX / validas, my = somaY / validas, mz = somaZ / validas;
    float dp = sqrtf(fmaxf(fmaxf(s2X / validas - mx * mx, s2Y / validas - my * my), s2Z / validas - mz * mz));
    if (dp < 1.0f && aMax - aMin < 0.05f) {   // so aceita se estava realmente imovel
      biasGx = mx; biasGy = my; biasGz = mz; ultimaCalibOk = true;
    } else {
      Serial.printf("CALIBRACAO RECUSADA: sensor em movimento (dp %.2f d/s). Bias anterior mantido.\n", dp);
    }
  }
  primeiroDado = true;
  somaAcc2 = somaGiro2 = 0;
  contagem = 0;
  proximaLeitura = millis();
  estadoAtual = PARADO;
  Serial.println(ultimaCalibOk ? "CALIBRACAO CONCLUIDA" : "CALIBRACAO NAO APLICADA");
}
// API: adicionarHistorico — Adiciona medição ao buffer circular de atividade do painel.
void adicionarHistorico(float acc, float giro, Estado estado) {
  historico[indiceHistorico] = { millis()/1000UL, acc, giro, (uint8_t)estado };
  indiceHistorico = (indiceHistorico + 1) % CAPACIDADE_HISTORICO;
  if (totalHistorico < CAPACIDADE_HISTORICO) totalHistorico++;
  sequencia++;
}
// API: indiceLogico — Converte posição lógica do histórico circular em índice físico.
uint16_t indiceLogico(uint16_t i) {
  return (indiceHistorico + CAPACIDADE_HISTORICO - totalHistorico + i) % CAPACIDADE_HISTORICO;
}
// API: enviarStatus — Responde JSON com status, bateria, leitura e indicadores de atividade.
void enviarStatus() {
  char json[640];
  char bat[20];
  if (MEDIR_BATERIA && bateriaVolts > 0.0f) snprintf(bat, sizeof(bat), "%.2f", bateriaVolts);
  else snprintf(bat, sizeof(bat), "null");
  char pctBuf[12];
  strcpy(pctBuf, "null"); // Sem medidor de carga dedicado. Compatibilidade API.
  snprintf(json, sizeof(json),
    "{\"seq\":%lu,\"uptime_s\":%lu,\"acc\":%.4f,\"giro\":%.3f,\"estado\":\"%s\",\"sensor\":%s,\"bat_v\":%s,\"bat_pct\":%s,\"bat_low\":%s,\"registros\":%u,\"flash_ok\":%s,\"flash_full\":%s,\"gravacao\":%s,\"rotulo\":\"%s\",\"parado_s\":%lu,\"leve_s\":%lu,\"intenso_s\":%lu,\"atrasadas\":%lu,\"calib_ok\":%s}",
    (unsigned long)sequencia, (unsigned long)(millis()/1000UL), accAtual, giroAtual,
    nomeEstado(estadoAtual), enderecoMPU ? "true" : "false", bat, pctBuf,
    (bateriaVolts>2.8f && bateriaVolts<3.55f)?"true":"false", totalHistorico,
    flashPronta ? "true" : "false", flashCheia ? "true" : "false",
    gravacaoAtiva ? "true" : "false", ROTULOS[rotuloHumano],
    (unsigned long)tempoEstados[0], (unsigned long)tempoEstados[1], (unsigned long)tempoEstados[2],
    (unsigned long)amostrasAtrasadas, ultimaCalibOk ? "true" : "false");
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8", json);
}
// API: enviarHistorico — Publica histórico recente em JSON para o gráfico.
void enviarHistorico() {
  servidor.setContentLength(CONTENT_LENGTH_UNKNOWN);
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8", "");
  servidor.sendContent("[");
  const uint16_t qtd = totalHistorico < 180 ? totalHistorico : 180;
  const uint16_t inicio = totalHistorico - qtd;
  char linha[96];
  for (uint16_t i = 0; i < qtd; i++) {
    const Medicao &m = historico[indiceLogico(inicio + i)];
    snprintf(linha, sizeof(linha), "%s{\"t\":%lu,\"a\":%.4f,\"g\":%.2f,\"e\":%u}",
      i ? "," : "", (unsigned long)m.tempoSegundos, m.acc, m.giro, (unsigned)m.estado);
    servidor.sendContent(linha);
  }
  servidor.sendContent("]");
  servidor.sendContent("");
}
// API: baixarCSV — Gera CSV para análise externa a partir das leituras recentes.
void baixarCSV() {
  servidor.sendHeader("Content-Disposition", "attachment; filename=chihuahua_ultimos15min.csv");
  servidor.setContentLength(CONTENT_LENGTH_UNKNOWN);
  servidor.send(200, "text/csv; charset=utf-8", "");
  servidor.sendContent("segundos_desde_inicio,acc_rms_g,giro_rms_graus_s,atividade\r\n");
  char linha[96];
  for (uint16_t i = 0; i < totalHistorico; i++) {
    const Medicao &m = historico[indiceLogico(i)];
    snprintf(linha, sizeof(linha), "%lu,%.4f,%.2f,%s\r\n",
      (unsigned long)m.tempoSegundos, m.acc, m.giro, nomeEstado((Estado)m.estado));
    servidor.sendContent(linha);
  }
  servidor.sendContent("");
}

// Registro prolongado na memoria interna (flash). Nao exige cartao SD.
// Usa a particao LittleFS existente na selecao de placa. Nao formata automaticamente.
// API: iniciarLogFlash — Abre sessão de CSV no LittleFS para coleta prolongada.
void iniciarLogFlash() {
  idSessao = esp_random();
  flashPronta = LittleFS.begin(false);
  if (!flashPronta) {
    Serial.println("AVISO: LittleFS indisponivel. CSV longo desativado; CSV de 15 min continua funcionando.");
    Serial.println("Verifique Tools > Partition Scheme para uma opcao com SPIFFS/FS, sem alterar antes de salvar sua v1.0.");
    return;
  }
  const bool jaExiste = LittleFS.exists(ARQUIVO_LOG_FLASH);
  File arq = LittleFS.open(ARQUIVO_LOG_FLASH, jaExiste ? FILE_APPEND : FILE_WRITE);
  if (!arq) { flashPronta = false; Serial.println("AVISO: nao consegui abrir CSV longo"); return; }
  if (!jaExiste || arq.size() == 0) arq.println("sessao,segundos_desde_ligado,acc_rms_g,giro_rms_graus_s,atividade,rotulo_observado");
  flashCheia = arq.size() >= LIMITE_LOG_FLASH_BYTES;
  arq.close();
  Serial.printf("LittleFS pronto. Arquivo: %s. Limite: %u bytes. Sessao: %lu\n",
     ARQUIVO_LOG_FLASH, (unsigned)LIMITE_LOG_FLASH_BYTES, (unsigned long)idSessao);
}
// API: registrarLogFlash — Persiste resumo amostrado no arquivo de sessão.
void registrarLogFlash(float acc, float giro, Estado estado) {
  if (!flashPronta || flashCheia || !gravacaoAtiva) return;
  const uint32_t agora = millis();
  if (ultimoLogFlash != 0 && agora - ultimoLogFlash < INTERVALO_LOG_FLASH_MS) return;
  ultimoLogFlash = agora;
  File arq = LittleFS.open(ARQUIVO_LOG_FLASH, FILE_APPEND);
  if (!arq) { flashPronta = false; Serial.println("ERRO: falha ao escrever no CSV longo"); return; }
  if (arq.size() >= LIMITE_LOG_FLASH_BYTES) {
    flashCheia = true;
    arq.close();
    Serial.println("AVISO: arquivo CSV chegou ao limite de espaco");
    return;
  }
  arq.printf("%lu,%lu,%.4f,%.2f,%s,%s\n", (unsigned long)idSessao,
      (unsigned long)(agora/1000UL), acc, giro, nomeEstado(estado), ROTULOS[rotuloHumano]);
  arq.close();  // garante escrita periodica mesmo se Wi-Fi cair
  linhasLogFlash++;
}
// API: baixarLogFlash — Disponibiliza CSV da sessão para download HTTP.
void baixarLogFlash() {
  if (!flashPronta || !LittleFS.exists(ARQUIVO_LOG_FLASH)) {
    servidor.send(503, "text/plain; charset=utf-8", "Historico em flash indisponivel. Use CSV curto.");
    return;
  }
  File arq = LittleFS.open(ARQUIVO_LOG_FLASH, FILE_READ);
  if (!arq) { servidor.send(500,"text/plain","Erro ao abrir arquivo"); return; }
  servidor.sendHeader("Content-Disposition", "attachment; filename=chihuahua_resumo.csv");
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.streamFile(arq, "text/csv; charset=utf-8");
  arq.close();
}

// Rotulos sao anotacoes de quem observa o animal, NAO classificacao por IA.
// API: registrarMarcacao — Registra anotação humana como evento separado do movimento bruto.
void registrarMarcacao(const char *tipo) {
  if (!flashPronta) return;
  File f = LittleFS.open(ARQUIVO_MARCACOES, FILE_APPEND);
  if (!f) return;
  if (f.size() == 0) f.println("sessao,segundos_desde_ligado,evento,rotulo_observado");
  f.printf("%lu,%lu,%s,%s\n", (unsigned long)idSessao,
    (unsigned long)(millis()/1000UL), tipo, ROTULOS[rotuloHumano]);
  f.close();
}
// API: baixarMarcacoes — Exporta as anotações do observador em CSV.
void baixarMarcacoes() {
  if (!flashPronta || !LittleFS.exists(ARQUIVO_MARCACOES)) {
    servidor.send(503, "text/plain; charset=utf-8", "Sem marcacoes; verifique LittleFS."); return;
  }
  File f = LittleFS.open(ARQUIVO_MARCACOES, FILE_READ);
  if (!f) { servidor.send(500,"text/plain","Erro ao abrir anotacoes"); return; }
  servidor.sendHeader("Content-Disposition", "attachment; filename=chihuahua_marcacoes.csv");
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.streamFile(f, "text/csv; charset=utf-8");
  f.close();
}

// API: iniciarRede — Ativa AP local e, opcionalmente, conexão a rede Wi-Fi existente.
void iniciarRede() {
  WiFi.persistent(false);
  const bool wifiCasa = strlen(WIFI_CASA_NOME) > 0;
  WiFi.mode(wifiCasa ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,8,1), IPAddress(192,168,8,1), IPAddress(255,255,255,0));
  if (!WiFi.softAP(WIFI_AP_NOME, WIFI_AP_SENHA)) {
    Serial.println("ERRO: nao consegui criar a rede Wi-Fi AP");
  }
  Serial.print("Wi-Fi: "); Serial.println(WIFI_AP_NOME);
  Serial.print("Dashboard: http://"); Serial.println(WiFi.softAPIP());
  if (wifiCasa) {
    WiFi.begin(WIFI_CASA_NOME, WIFI_CASA_SENHA);
    uint32_t inicio = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < 12000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("IP na rede de casa: http://"); Serial.println(WiFi.localIP());
    } else Serial.println("Rede de casa indisponivel. Use ChihuahuaLab-001");
  }
  servidor.on("/", HTTP_GET, [](){ servidor.send_P(200, "text/html; charset=utf-8", PAGINA); });
  servidor.on("/api/status", HTTP_GET, enviarStatus);
  servidor.on("/api/history", HTTP_GET, enviarHistorico);
  servidor.on("/download.csv", HTTP_GET, baixarCSV);
  servidor.on("/download-full.csv", HTTP_GET, baixarLogFlash);
  servidor.on("/download-labels.csv", HTTP_GET, baixarMarcacoes);
  servidor.on("/api/record/start", HTTP_POST, [](){
    if (!flashPronta || flashCheia) { servidor.send(503, "text/plain", "LittleFS indisponivel ou limite atingido"); return; }
    idSessao = esp_random(); gravacaoAtiva = true; ultimoLogFlash = 0;
    registrarMarcacao("INICIO_SESSAO");
    servidor.send(200, "text/plain", "Gravacao da sessao iniciada");
  });
  servidor.on("/api/record/stop", HTTP_POST, [](){
    if (gravacaoAtiva) registrarMarcacao("FIM_SESSAO");
    gravacaoAtiva = false;
    servidor.send(200, "text/plain", "Gravacao parada. Baixe os CSVs.");
  });
  servidor.on("/api/label", HTTP_POST, [](){
    if (!servidor.hasArg("code")) { servidor.send(400,"text/plain","Parametro code ausente"); return; }
    int i = servidor.arg("code").toInt();
    if (i < 0 || i > 7) { servidor.send(400,"text/plain","Rotulo invalido"); return; }
    rotuloHumano = (uint8_t)i;
    registrarMarcacao("ROTULO_HUMANO");
    servidor.send(200,"text/plain",String("Observacao registrada: ")+ROTULOS[rotuloHumano]);
  });
  servidor.on("/api/calibrate", HTTP_POST, [](){
    if (!enderecoMPU) { servidor.send(409, "text/plain", "Sensor desconectado"); return; }
    solicitarCalibracao = true;
    servidor.send(202, "text/plain", "Calibracao solicitada");
  });
  servidor.onNotFound([](){ servidor.send(404, "text/plain", "Pagina nao encontrada"); });
  servidor.begin();
  Serial.println("Servidor HTTP iniciado");
}
// API: lerBateriaSeAtivada — Atualiza leitura de bateria quando a opção de medição está ativa.
void lerBateriaSeAtivada() {
  if (!MEDIR_BATERIA || (ultimoADC && millis() - ultimoADC < 10000)) return;
  ultimoADC = millis();
  digitalWrite(ADC_CTRL, HIGH);
  delay(2);
  uint32_t somaMv = 0;
  for (int i=0; i<8; i++) { somaMv += analogReadMilliVolts(ADC_VBAT); delay(2); }
  digitalWrite(ADC_CTRL, LOW);
  const float leitura = (somaMv / 8.0f) * 4.9f / 1000.0f;
  if (leitura >= 3.0f && leitura <= 4.3f) {
    bateriaVolts = bateriaVolts < 0.0f ? leitura : 0.8f * bateriaVolts + 0.2f * leitura;
    Serial.printf("BAT %.3f V filtrados | carga %% desconhecida | USB pode alterar leitura\n", bateriaVolts);
  } else {
    Serial.println("BAT: amostra fora da faixa - verificar circuito e bateria");
  }
}

#if HABILITAR_LORA_TX
// API: iniciarLoRa — Configura a transmissão experimental LoRa específica do Chihuahua.
void iniciarLoRa() {
  SPI.begin(9, 11, 10, 8); // SCK MISO MOSI SS na Heltec V3.2
#if defined(RADIOLIB_VERSION_MAJOR) && (RADIOLIB_VERSION_MAJOR >= 8)
  radio.tcxoVoltage = RadioLibTCXOVoltage_t::Voltage1V8;   // RadioLib 8.x
  ConfigLoRa_t cfg; cfg.frequency = RADIOLIB_UNIT_MEGA(915); cfg.bandwidth = RADIOLIB_UNIT_KILO(125);
  cfg.spreadingFactor = 9; cfg.codingRate = 7; cfg.syncWord = 0x12; cfg.power = 10; cfg.preambleLength = 8;
  int16_t rc = radio.begin(cfg);
#else
  int16_t rc = radio.begin(915.0f, 125.0f, 9, 7, 0x12, 10, 8, 1.8f, false); // RadioLib 7.x
#endif
  loraPronto = (rc == RADIOLIB_ERR_NONE);
  if (loraPronto) { radio.setDio1Action(loraIsr); radio.sleep(); }
  Serial.printf("LoRa SX1262 inicializacao: %d\n", (int)rc);
}
// API: enviarLoRaSeAtivado — Envia estado quando LoRa está habilitado; formato distinto do protocolo bovino v5.
void enviarLoRaSeAtivado() {
  if (!loraPronto) return;
  if (loraFim) { loraFim = false; radio.finishTransmit(); radio.sleep(); loraOcupado = false; }
  if (loraOcupado || sequencia == 0 || millis() - ultimaTx < 5000) return;
  ultimaTx = millis();
  static char payload[80];  // static: o radio le o buffer durante a transmissao
  snprintf(payload, sizeof(payload), "DOG001,%lu,%s,%.3f,%.1f",
     (unsigned long)(millis()/1000UL), nomeEstado(estadoAtual), accAtual, giroAtual);
  int16_t rc = radio.startTransmit((uint8_t *)payload, strlen(payload)); // nao bloqueia o loop
  loraOcupado = (rc == RADIOLIB_ERR_NONE);
  Serial.printf("LoRa TX iniciado rc=%d payload=%s\n", (int)rc, payload);
}
#endif

// API: setup — Prepara sensor, tela, rede, sistema de arquivos e rotas.
void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("\nCHIHUAHUA LAB v1.5 - TESTE SUPERVISIONADO");
  iniciarOLED();
  if (MEDIR_BATERIA) {pinMode(ADC_CTRL, OUTPUT); digitalWrite(ADC_CTRL, LOW); analogReadResolution(12); lerBateriaSeAtivada();}
  iniciarLogFlash();
  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setClock(100000);
  iniciarRede();
#if HABILITAR_LORA_TX
  iniciarLoRa();
#else
  Serial.println("LoRa TX: DESATIVADO (precisa de segundo receptor para teste)");
#endif
  Serial.println("Dashboard Chihuahua: http://192.168.8.1");
  Serial.println("Pronto. Deixe o sensor parado na calibracao.");
}

// API: loop — Atende painel e classifica leituras de movimento em testes supervisionados.
void loop() {
  servidor.handleClient();
  lerBateriaSeAtivada();
#if HABILITAR_LORA_TX
  enviarLoRaSeAtivado();
#endif
  if (!enderecoMPU) {
    if (millis() - ultimaTentativa < 1500) { delay(1); return; }
    ultimaTentativa = millis();
    if (!iniciarSensor()) {
      estadoAtual = ERRO_MPU;
      mostrarTela("ERRO MPU", 0, 0, false);
      Serial.println("MPU6050 NAO DETECTADO - tentando reconectar");
      delay(1);
      return;
    }
    Serial.println("MPU6050 DETECTADO!");
    calibrarParado();
  }
  if (solicitarCalibracao) {
    solicitarCalibracao = false;
    calibrarParado();
  }
  if ((int32_t)(millis() - proximaLeitura) < 0) { delay(1); return; }
  // ritmo fixo: proxima = anterior + 50 ms (nao acumula atraso). Se atrasou muito, ressincroniza.
  if ((int32_t)(millis() - proximaLeitura) > (int32_t)INTERVALO_AMOSTRA_MS) { amostrasAtrasadas++; proximaLeitura = millis(); }
  proximaLeitura += INTERVALO_AMOSTRA_MS;
  float ax, ay, az, gx, gy, gz;
  if (!lerSensor(ax, ay, az, gx, gy, gz)) {
    Serial.println("ERRO DE LEITURA - reconectando MPU");
    enderecoMPU = 0;
    estadoAtual = ERRO_MPU;
    mostrarTela("ERRO MPU", 0, 0, false);
    return;
  }
  if (primeiroDado) {
    gravX = ax; gravY = ay; gravZ = az; primeiroDado = false;
  }
  const float alpha = 0.94f;
  gravX = alpha*gravX + (1-alpha)*ax;
  gravY = alpha*gravY + (1-alpha)*ay;
  gravZ = alpha*gravZ + (1-alpha)*az;
  const float dx=ax-gravX, dy=ay-gravY, dz=az-gravZ;
  const float rx=gx-biasGx, ry=gy-biasGy, rz=gz-biasGz;
  somaAcc2 += dx*dx + dy*dy + dz*dz;
  somaGiro2 += rx*rx + ry*ry + rz*rz;
  contagem++;
  if (contagem >= AMOSTRAS_POR_JANELA) {
    accAtual = sqrtf(somaAcc2 / contagem);
    giroAtual = sqrtf(somaGiro2 / contagem);
    if (accAtual < 0.035f && giroAtual < 5.0f) estadoAtual = PARADO;
    else if (accAtual >= 0.18f || giroAtual >= 45.0f) estadoAtual = INTENSO;
    else estadoAtual = LEVE;
    if (estadoAtual <= INTENSO) tempoEstados[(uint8_t)estadoAtual]++;
    adicionarHistorico(accAtual, giroAtual, estadoAtual);
    registrarLogFlash(accAtual, giroAtual, estadoAtual);
    mostrarTela(nomeEstado(estadoAtual), accAtual, giroAtual, true);
    Serial.printf("%.3f g | %.1f graus/s | %s | seq=%lu\n",
      accAtual, giroAtual, nomeEstado(estadoAtual), (unsigned long)sequencia);
    somaAcc2 = somaGiro2 = 0;
    contagem = 0;
  }
}
