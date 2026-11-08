# Publicar no GitHub — repositório público existente

Destino: **https://github.com/jsbrcadtech/Smart-Collar**. O repositório já foi criado pelo proprietário como público; não crie outro e não faça push forçado.

## Antes do envio

1. Revise os arquivos e as instruções de [segurança](SEGURANCA.md).
2. Os `.ino` utilizam `*_config_local.h` com senha local. Esses arquivos nunca devem ir para o GitHub. Apenas os modelos `.example.h` são rastreados.
3. **Não adicione plano de negócios, contatos, preços, possíveis parceiros ou dados individuais de animais**. Esses conteúdos ficam em Google Drive com acesso controlado.
4. Execute `python scripts/verificar-repo.py`, verifique o diff e compile a versão sanitizada com `scripts/compilar.sh` ou GitHub Actions.
5. A compilação original v3.1.3 antecede a separação das senhas; é necessária uma nova execução para verificar os fontes desta publicação.

## Publicar com GitHub CLI, se necessário

No PowerShell, dentro da pasta `smart-collar` já extraída, execute:

```powershell
.\scripts\PUBLICAR_NO_GITHUB.ps1
```

O script usa a branch `main` sem `--force`. Não cria um repositório novo. Se o GitHub já tiver conteúdo, revise o histórico antes de tentar integrar mudanças locais.

## Colaboração

Na página do repositório, entre em **Settings > Collaborators > Add people** e convide seu amigo pelo nome de usuário GitHub. Ele precisa aceitar o convite. Trabalhem em branches próprias e usem PRs; o plano comercial compartilhado fica no Drive.

**Atenção:** o ZIP de origem v3.1.3 contém senhas de desenvolvimento hardcoded e não deve ser publicado. Não ligue rádio LoRa sem antena.
