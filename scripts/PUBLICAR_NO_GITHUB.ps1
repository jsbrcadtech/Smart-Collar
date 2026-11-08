# Envia a pasta atual para o repositório público EXISTENTE jsbrcadtech/Smart-Collar.
# Requer Git for Windows + GitHub CLI. Não cria repositório nem força atualizações.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Instale Git for Windows.' }
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'Instale GitHub CLI.' }
& gh auth status
if ($LASTEXITCODE -ne 0) { throw 'Conecte o GitHub com gh auth login --web' }
& gh repo view jsbrcadtech/Smart-Collar
if ($LASTEXITCODE -ne 0) { throw 'Repositorio jsbrcadtech/Smart-Collar nao encontrado.' }
& python scripts/verificar-repo.py
if ($LASTEXITCODE -ne 0) { throw 'Revisao de seguranca falhou.' }
if (-not (Test-Path '.git')) { & git init -b main }
& git remote get-url origin 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { & git remote add origin https://github.com/jsbrcadtech/Smart-Collar.git }
& git add .
if ($LASTEXITCODE -ne 0) { throw 'git add falhou' }
& git diff --cached --name-only
Write-Host 'Confira os arquivos acima antes de publicar num repositório PUBLICO.'
& git commit -m 'Documenta Smart Collar v3.1.3, Base v1.4 e Chihuahua v1.5'
if ($LASTEXITCODE -ne 0) { throw 'git commit falhou. Configure user.name/email se necessário.' }
& git push -u origin main
if ($LASTEXITCODE -ne 0) { throw 'Push falhou. Verifique autenticacao e alteracoes remotas, sem usar force.' }
Write-Host 'Publicado: https://github.com/jsbrcadtech/Smart-Collar'
