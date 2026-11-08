# Smart Collar — pesquisa de comportamento animal

Protótipo de monitoramento com **Heltec WiFi LoRa 32 V3.2 (ESP32-S3 + SX1262)** e **MPU6050 (GY-521)**. Repositório organizado para colaboração e revisão de código.

> **Estado atual:** compilação da versão de origem v3.1.3 registrada em logs locais; versões deste repositório tiveram as credenciais externalizadas e **ainda precisam ser compiladas e testadas fisicamente**. Não usar como dispositivo veterinário ou sistema de segurança.

## Projetos independentes

| Diretório | Versão | Objetivo |
|---|---|---|
| [`firmware/bovinos/SmartCollar_Research_v313`](firmware/bovinos/SmartCollar_Research_v313) | v3.1.3 | Coleta bruta e agregada; regras experimentais de atividade, cabeça baixa, ritmo e passos; painel e LoRa opcional |
| [`firmware/base-lora/SmartCollar_Base_v14`](firmware/base-lora/SmartCollar_Base_v14) | v1.4 | Estação-base LoRa, ACK após registro persistente (ressalvas no STATUS), CSV e painel |
| [`firmware/chihuahua/ChihuahuaLab_v15`](firmware/chihuahua/ChihuahuaLab_v15) | v1.5 | Teste supervisionado separado com coleta de atividade e observações humanas |

**Compatibilidade:** colar bovino v3.1.3 + Base v1.4 usam o **protocolo LoRa v5** e devem ser gravados em conjunto. A transmissão experimental do Chihuahua usa outro formato de mensagem.

## Comece aqui

1. Leia [segurança e limites](docs/SEGURANCA.md) e [instalação e compilação](docs/COMPILACAO.md).
2. Copie cada `*_config_local.example.h` para `*_config_local.h` na mesma pasta do sketch, defina senhas diferentes e substitua `SMARTCOLLAR_CONFIG_PRONTA 0` por `1` **no arquivo local**. Os arquivos locais são ignorados pelo Git.
3. Abra a pasta do sketch desejado no Arduino IDE, selecione **Heltec WiFi LoRa 32 (V3)**, ou compile com `arduino-cli`.
4. Veja as [ligações elétricas](hardware/LIGACOES.md) e o [roteiro de testes](docs/TESTES_BANCADA.md).

## Páginas de referência

- [Arquitetura e comunicação](docs/ARQUITETURA.md)
- [Guia de leitura do código comentado](docs/DOCUMENTACAO_CODIGO.md)
- [Funcionalidades testáveis](docs/FUNCIONALIDADES.md)
- [Status e limitações conhecidas](docs/STATUS.md)
- [Roadmap](docs/ROADMAP.md)
- [Como contribuir](CONTRIBUTING.md)
- [Compilações de referência](docs/COMPILACAO.md)

**Nunca transmita LoRa sem antena conectada.** Não repita a instrução insegura do teste 11a presente em uma versão antiga do pacote.

Código visível publicamente, **sem licença open-source por enquanto**: direitos reservados; visitantes não recebem autorização automática para reutilizar/redistribuir. A licença futura deve ser discutida entre os colaboradores.

## Business plan / Plano de negócios

Documentos comerciais em português e inglês são mantidos separadamente em armazenamento compartilhado com acesso controlado (por exemplo, Google Drive). **Não ficam neste GitHub público.**