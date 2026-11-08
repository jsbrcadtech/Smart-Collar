param(
  [Parameter(Mandatory=$true)]
  [ValidatePattern('^[A-Za-z0-9-]{1,39}$')]
  [string]$UsuarioGitHub
)
$ErrorActionPreference='Stop'
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'Instale GitHub CLI: https://cli.github.com/' }
& gh auth status
if ($LASTEXITCODE -ne 0) { throw 'Conecte-se antes com gh auth login --web' }
& gh api --method PUT "repos/jsbrcadtech/Smart-Collar/collaborators/$UsuarioGitHub" -f 'permission=push'
if ($LASTEXITCODE -ne 0) { throw 'Falha no convite. Confirme usuario e permissoes do repositorio.' }
Write-Host "Convite para $UsuarioGitHub solicitado. Ele precisa aceitar no GitHub."
