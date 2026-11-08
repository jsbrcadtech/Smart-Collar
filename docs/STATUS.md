# Estado do desenvolvimento — v3.1.3 / Base v1.4 / Chihuahua v1.5

**Entregue:** fontes organizados, configurações Wi-Fi removidas dos sketches, script de compilação, testes de bancada e documentação para colaboração.

**Não comprovado nesta preparação:** recompilação dos fontes após a externalização de credenciais; testes em Heltec física; comportamento real em bovinos/cães; alcance LoRa, autonomia e robustez sob falhas.

Os logs de compilação da distribuição de origem v3.1.3 documentavam 9 compilações sem erros/avisos para os fontes anteriores à separação das credenciais. O script foi atualizado para esta estrutura; ele deve ser executado no ambiente do colaborador ou no CI.

## Riscos conhecidos herdados da quinta revisão

1. `fecharHora()` não usa o resultado de persistência para bloquear substituição do checkpoint caso LittleFS **e** NVS falhem simultaneamente; pode haver perda.
2. Na base v1.4, quando `fsOk` é falso, o modo Serial pode enviar ACK sem armazenamento durável. Não tratar todo ACK como prova universal de persistência.
3. `flushRaw()` não verifica a quantidade de bytes retornados por `write()` antes de zerar o buffer.
4. O mecanismo de sincronização bruta guarda apenas um marcador pendente; várias sincronizações durante fila atrasada podem sobrescrever o evento anterior.
5. A matriz de logs de origem não cobria `HABILITAR_LORA=1` e `HABILITAR_DEBUG=1` simultaneamente; o CI deste repo inclui essa combinação.
6. O watchdog do loop, isoladamente, não prova que a tarefa de aquisição está progredindo.

**Não implementar novas funcionalidades sem primeiro testar e corrigir integridade de dados.** Em caso de erro de gravação, falhas precisam ser visíveis — nunca inferir completude do CSV apenas pela compilação.
