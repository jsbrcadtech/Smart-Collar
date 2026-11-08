# Funcionalidades presentes para teste

## Bovinos — SmartCollar Research v3.1.3

- IMU (3 eixos de aceleração + 3 eixos de giro), FIFO, leitura nominal a 20 Hz, contadores de lacunas/descartes.
- Intensidade PARADO / LEVE / INTENSO e candidatos de comportamento: PARADO, LOCOMOCAO, CABECA_BAIXA, RITMO_MASTIGACAO e OUTRO_ATIVO.
- Calibração do giroscópio, referência de cabeça normal/baixa, pitch/roll, tempo de cabeça baixa e ritmo periódico candidato a mastigação.
- Estimativa de passos, picos de impacto, índice experimental de atividade baseado na mesma hora dos dias anteriores.
- Painel Wi-Fi `192.168.4.1`, OLED, relógio sincronizado pelo navegador, botões/LED de sincronização de vídeo.
- Rotulagem manual temporal para futura criação de dados supervisionados.
- Capturas brutas de 3/10/30/60 min, downloads CSV, resumos de 10 s, eventos, horas, gráficos de 24 h.
- LittleFS, checkpoint em dois slots, NVS para horas pendentes, monitoração de bateria, watchdog, diagnóstico e reconexão do MPU.
- Radio LoRa (opcional) com resumos, contadores acumulados, ACK/retransmissão, `boot_id`.
- Testes debug protegidos por compile flag: atraso de fila, flash cheia, hora curta, offset de tempo.

## Base LoRa v1.4

- Recepção/ACK (quando gravado), identificação de até 16 colares, RSSI/SNR, contadores cumulativos e reconciliação de intervalos.
- CSV com checksum de linha, recuperação do CSV e rotação de logs, painel Wi-Fi `192.168.5.1`.
- Compatível com **bovinos v3.1.3 + LoRa v5**, não com formato experimental Chihuahua.

## Chihuahua Lab v1.5

- MPU6050, intensidade PARADO/LEVE/INTENSO, amostragem nominal 20 Hz, calibração e diagnóstico.
- OLED e painel Wi-Fi `192.168.8.1`; monitor de bateria, rótulos manuais e CSVs.
- Transmissão LoRa experimental opcional, sem integração direta com a base bovina atual.

Todas as detecções de comportamento animal são **experimentais** até validação física e por vídeo. Compilação não prova precisão, integridade de dados nem autonomia.
