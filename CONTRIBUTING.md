# Colaboração

## Fluxo recomendado

1. Crie uma branch com nome descritivo, por exemplo `fix/checkpoint-recovery` ou `feature/rotulos`.
2. Não comite `*_config_local.h`, senhas, dados brutos de animais, endereços pessoais ou logs de compilação que exponham caminhos locais.
3. Execute a compilação e os testes relacionados à mudança (ver `docs/COMPILACAO.md`).
4. Abra um **Pull Request** com descrição, firmware afetado, evidências de compilação/teste e eventuais riscos de integridade dos dados.
5. Solicite revisão antes de fazer merge na branch `main`.

**Critérios mínimos:** sem avisos do compilador; compatibilidade do LoRa v5 preservada ou versionada; testes de recuperação para qualquer alteração em LittleFS/NVS; confidencialidade de credenciais e gravações respeitada.

### Convite do amigo

No repositório público existente, vá a **Settings → Collaborators and teams → Add people**, pesquise o usuário GitHub dele e conceda acesso adequado (normalmente write). O convite precisa ser aceito. Alternativamente, use `scripts/convidar-colaborador.ps1` com o login GitHub dele depois de autenticar o `gh`.

> Recomendação: mantenha `main` para versões revisadas; todos os testes físicos devem documentar data, configuração, placa, versão e resultado. Não compartilhe dados pessoais ou de localização de fazendas sem autorização.

## Repositório público

Discussões de custos, margens, fazendas parceiras, clientes potenciais e documentos de negócio não devem ser criadas como Issues públicas. Use o espaço comercial compartilhado com acesso restrito.
