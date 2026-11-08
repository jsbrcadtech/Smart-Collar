# Testes de bancada

**Sempre com antenas conectadas se LoRa estiver habilitado.** NÃO seguir a instrução insegura de desconectar antena que constava na versão 3.1.2.

O roteiro de origem foi preservado em [`LEIA_PRIMEIRO_v313_ORIGINAL.txt`](LEIA_PRIMEIRO_v313_ORIGINAL.txt), incluindo os testes 1–21, parâmetros e resultados esperados. Abaixo, um resumo para registrar em issues/PRs.

| Grupo | Testes | Evidência esperada |
|---|---|---|
| Sensores/CSV | 1–8 | MPU reconhecido, dados na frequência esperada, sincronização e CSV íntegro |
| Persistência/rotulagem | 9–10, 18–21 | checkpoints e rótulos corretos; nenhum segmento duplicado |
| LoRa | 11a–11c, 17 | intervalo reconstruído e reinícios diferenciados; antenas conectadas |
| Recuperação MPU | 12 | erro e reconexão visíveis, marcação de lacunas |
| Viradas de tempo | 15–16 | timestamps monotônicos, fila sendo consumida |
| Flash cheia | 21 | fila NVS usada e recuperada após liberação |

**Limitações:** não há teste físico executado documentado nesta versão. Documente perdas e falhas, não apenas testes aprovados. Os números 13/14 foram consolidados/renumerados no roteiro de origem; siga o texto do teste, não apenas o número.
