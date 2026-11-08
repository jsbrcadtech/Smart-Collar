# Segurança, privacidade e credenciais

- **Nunca transmita com a antena desconectada.** Não reproduza o antigo teste 11a. Para simular perda de recepção, use distância/atenuação adequada, mantendo antenas conectadas.
- Sistema de **pesquisa experimental**, não instrumento veterinário, detector clínico de cio/doença ou medidor real de ingestão.
- Configure **senhas Wi-Fi distintas e fortes** em cada `*_config_local.h` (ignorado pelo Git). Os valores em `*.example.h` são apenas marcadores e o arquivo de exemplo impede compilação até ser alterado.
- O painel é servido por HTTP local, sem TLS; não encaminhe portas nem exponha o AP à Internet. Quem tiver acesso à rede local pode acessar endpoints do firmware.
- O protocolo LoRa v5, como fornecido, **não oferece criptografia/autenticação** dos pacotes; não inclua dados pessoais nas IDs nem considere a comunicação privada ou à prova de adulteração.
- Nunca inclua CSVs de animais, vídeos, dados de fazendas, arquivos de NVS/LittleFS ou logs sensíveis no repositório sem autorização.
- Não comite chaves, tokens, senhas ou o arquivo `*_config_local.h` mesmo em repositório privado. Se alguma senha do pacote original já estiver em uso, **troque-a**.
- Faça testes na bancada antes de fixar dispositivos em animais. Proteja cabos, bateria e caixas; não use durante atividades em que o dispositivo possa causar ferimentos.
