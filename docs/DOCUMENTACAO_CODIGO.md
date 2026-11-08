# Guia de leitura da codebase

Os comentários `// API:` foram adicionados para documentar a intenção das rotinas, **sem mudar a lógica interna**. Consulte também os comentários originais das versões upstream.

## Bovinos (v3.1.3)

1. **Aquisição:** `iniciarMPU()` configura MPU6050; `tarefaSensor()` lê FIFO e alimenta fila de RAM.
2. **Processamento:** `servicoFila()` passa leituras para `processarAmostra()`, que calcula intensidade, orientação, ritmo e candidatos heurísticos.
3. **Anotações:** `mudarRotulo()` registra eventos; `rotuloEm()` resolve rótulo pelo timestamp original da amostra.
4. **Persistência:** `fimJanela()` agrega blocos; `fecharHora()`, `persistirHora()`, `salvarCheckpointHora()` e `servicoPendente()` gerem CSV, checkpoint e NVS.
5. **Raw:** `iniciarRaw()`, `registrarRaw()`, `escreverMarcadorSeDevido()` e `enviarRawCSV()` cuidam de captura/exportação.
6. **Conectividade:** `registrarRotas()` expõe painel; `servicoLoRa()` opera transceptor v5.

## Base v1.4

`receber()` é o caminho crítico: valida pacote, identifica sessão `boot_id`, reconstrói intervalos, chama `gravarLog()` e então envia ACK. Na inicialização, `prepararArquivos()` e `aplicarLinha()` recuperam referências do CSV.

## Chihuahua v1.5

`iniciarSensor()` configura MPU; `loop()` classifica movimento; `registrarMarcacao()` permite observação supervisionada; `enviarStatus()`/`enviarHistorico()` servem a interface web. LoRa experimental do Chihuahua **não** é o protocolo v5 bovino.

## Limitações conhecidas (não corrigidas nesta publicação)

Veja [`STATUS.md`](STATUS.md). A presença de comentário não significa garantia de segurança, persistência ou validação veterinária.
