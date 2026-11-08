# Roadmap proposto

## Fase A — Bancada / qualidade
- [ ] Criar e configurar 3 arquivos `*_config_local.h` fora do versionamento.
- [ ] Recompilar cada firmware após esta reorganização.
- [ ] Corrigir os pontos abertos em `STATUS.md` (perda em falha simultânea, ACK em modo Serial, `flushRaw`, múltiplos marcadores de tempo).
- [ ] Passar testes 1–21; documentar lacunas, CRC e resultados dos CSVs.
- [ ] Coleta supervisionada de 8–12 horas; inspeção dos CSVs e logs.

## Fase B — Dados e validação
- [ ] Filmar comportamento, sincronizar com LED e rotular sessões.
- [ ] Conferir rótulos por timestamp e medir sensibilidade/especificidade dos candidatos.
- [ ] Separar animais e sessões nos conjuntos de treino/teste; não misturar segmentos do mesmo vídeo.

## Fase C — Comunicação / energia
- [ ] Medir perdas e alcance LoRa com duas Heltec.
- [ ] Medir consumo, revisar hardware de bateria e autonomia real.
- [ ] Definir autenticação/criptografia do rádio antes de qualquer piloto com dados sensíveis.

## Fase D — Produto experimental
- [ ] Avaliar sensores adicionais e um classificador pequeno para ESP32-S3.
- [ ] Projetar invólucro, fixação segura e protocolo de manutenção para testes com animais.
