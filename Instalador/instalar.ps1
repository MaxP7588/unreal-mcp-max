<#
Instalador de unreal-mcp-max.

  Modo simple   : copia el plugin ya compilado (no hace falta Visual Studio) y activa el servidor
                  sin las herramientas riesgosas (execute_python, console_command, delete_asset).
  Modo completo : copia el código fuente para compilarlo con el proyecto y activa todas las herramientas.

Uso:
  instalar.ps1                                  (pregunta todo)
  instalar.ps1 -Modo simple -Proyecto "C:\Ruta\MiProyecto.uproject"
#>
param(
    [ValidateSet("simple", "completo")]
    [string]$Modo,
    [string]$Proyecto
)

$ErrorActionPreference = "Stop"
$ServerName = "unrealMCP"

function Write-Paso($texto) { Write-Host ""; Write-Host "==> $texto" -ForegroundColor Cyan }
function Write-Ok($texto) { Write-Host "    $texto" -ForegroundColor Green }
function Write-Aviso($texto) { Write-Host "    $texto" -ForegroundColor Yellow }

function Stop-ConError($texto) {
    Write-Host ""
    Write-Host "ERROR: $texto" -ForegroundColor Red
    Write-Host ""
    exit 1
}

function Write-JsonSinBom($ruta, $objeto) {
    $json = $objeto | ConvertTo-Json -Depth 20
    [System.IO.File]::WriteAllText($ruta, $json, (New-Object System.Text.UTF8Encoding($false)))
}

# Agrega (o reemplaza) el servidor en un archivo de configuración MCP sin tocar los demás servidores.
function Set-ServidorMcp($ruta, $servidor) {
    $config = New-Object PSObject
    if (Test-Path $ruta) {
        $texto = [System.IO.File]::ReadAllText($ruta)
        if ($texto.Trim()) {
            try { $config = $texto | ConvertFrom-Json }
            catch {
                Write-Aviso "No se pudo leer $ruta (JSON inválido). No se modificó; agrega el servidor a mano."
                return $false
            }
        }
        Copy-Item $ruta "$ruta.bak" -Force
    }
    if (-not ($config.PSObject.Properties.Name -contains "mcpServers")) {
        $config | Add-Member -NotePropertyName mcpServers -NotePropertyValue (New-Object PSObject)
    }
    $config.mcpServers | Add-Member -NotePropertyName $ServerName -NotePropertyValue $servidor -Force
    New-Item -ItemType Directory -Force (Split-Path $ruta) | Out-Null
    Write-JsonSinBom $ruta $config
    return $true
}

function Find-Uv {
    $cmd = Get-Command uv -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($ruta in @("$env:USERPROFILE\.local\bin\uv.exe", "$env:USERPROFILE\.cargo\bin\uv.exe", "$env:LOCALAPPDATA\Programs\uv\uv.exe")) {
        if (Test-Path $ruta) { return $ruta }
    }
    return $null
}

$Raiz = Split-Path -Parent $PSScriptRoot
$PluginOrigen = Join-Path $Raiz "UnrealMCP"
$UpluginOrigen = Join-Path $PluginOrigen "UnrealMCP.uplugin"
if (-not (Test-Path $UpluginOrigen)) {
    Stop-ConError "No se encontró la carpeta UnrealMCP junto al instalador. Descomprime el zip completo y vuelve a intentarlo."
}

Write-Host ""
Write-Host "  Unreal MCP Max - instalador" -ForegroundColor White
Write-Host "  -------------------------------------------"

# --- Modo ---------------------------------------------------------------
if (-not $Modo) {
    Write-Host ""
    Write-Host "  1) Simple   - recomendado para estudiantes. No necesita Visual Studio."
    Write-Host "  2) Completo - todas las herramientas; compila el plugin con tu proyecto (requiere Visual Studio)."
    $opcion = Read-Host "  Elige 1 o 2 [1]"
    $Modo = if ($opcion -eq "2") { "completo" } else { "simple" }
}
Write-Ok "Modo: $Modo"

# --- Proyecto -----------------------------------------------------------
Write-Paso "Elige tu proyecto de Unreal (.uproject)"
if (-not $Proyecto) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dialogo = New-Object System.Windows.Forms.OpenFileDialog
        $dialogo.Title = "Elige el archivo .uproject de tu proyecto"
        $dialogo.Filter = "Proyecto de Unreal (*.uproject)|*.uproject"
        if ($dialogo.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $Proyecto = $dialogo.FileName }
    } catch { }
}
if (-not $Proyecto) {
    $Proyecto = (Read-Host "  Pega la ruta del archivo .uproject").Trim('"', ' ')
}
if (-not (Test-Path $Proyecto) -or [System.IO.Path]::GetExtension($Proyecto) -ne ".uproject") {
    Stop-ConError "No es un archivo .uproject válido: $Proyecto"
}
$Proyecto = (Resolve-Path $Proyecto).Path
$CarpetaProyecto = Split-Path -Parent $Proyecto
Write-Ok $Proyecto

# --- Versión de Unreal --------------------------------------------------
$uproject = [System.IO.File]::ReadAllText($Proyecto) | ConvertFrom-Json
$uplugin = [System.IO.File]::ReadAllText($UpluginOrigen) | ConvertFrom-Json
$versionProyecto = "$($uproject.EngineAssociation)"
$versionPlugin = "$($uplugin.EngineVersion)"
$DllOrigen = Join-Path $PluginOrigen "Binaries\Win64\UnrealEditor-UnrealMCP.dll"

if ($Modo -eq "simple") {
    if (-not (Test-Path $DllOrigen)) {
        Stop-ConError ("Esta carpeta no trae el plugin compilado. Para el modo simple descarga el zip de la " +
            "página 'Releases' de GitHub (no el botón 'Code'), o usa el modo completo.")
    }
    if ($versionPlugin -and $versionProyecto -match '^\d+\.\d+$') {
        $versionPluginCorta = ($versionPlugin -split '\.')[0..1] -join '.'
        if ($versionPluginCorta -ne $versionProyecto) {
            Stop-ConError ("Tu proyecto usa Unreal $versionProyecto, pero este plugin está compilado para Unreal $versionPluginCorta. " +
                "Descarga el zip de tu versión en la página 'Releases', o usa el modo completo.")
        }
    } elseif ($versionProyecto -notmatch '^\d+\.\d+$') {
        Write-Aviso "El proyecto usa un Unreal compilado desde código ($versionProyecto). Si no carga, usa el modo completo."
    }
} elseif (-not (Test-Path (Join-Path $CarpetaProyecto "Source"))) {
    Write-Aviso "Tu proyecto es solo Blueprint. En modo completo Unreal pedirá compilar el plugin y necesitas Visual Studio con 'Desarrollo de juegos con C++'."
    Write-Aviso "Si no sabes qué es esto, cancela (Ctrl+C) y usa el modo simple."
}
Write-Ok "Unreal $versionProyecto"

if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    Write-Aviso "Unreal Editor está abierto. Ciérralo antes de continuar (si no, no se puede reemplazar el plugin)."
    Read-Host "  Presiona Enter cuando lo hayas cerrado" | Out-Null
}

# --- Copiar el plugin ---------------------------------------------------
Write-Paso "Copiando el plugin a $CarpetaProyecto\Plugins\UnrealMCP"
$PluginDestino = Join-Path $CarpetaProyecto "Plugins\UnrealMCP"
if ((Resolve-Path $PluginOrigen).Path -eq $PluginDestino) {
    Stop-ConError "El instalador está dentro del proyecto. Descomprime el zip en otra carpeta (por ejemplo Descargas)."
}
if (Test-Path $PluginDestino) {
    # La copia de seguridad va a Saved/: dentro de Plugins/ Unreal la cargaría como un plugin duplicado.
    $respaldo = Join-Path $CarpetaProyecto ("Saved\UnrealMCP-respaldo-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    New-Item -ItemType Directory -Force (Split-Path $respaldo) | Out-Null
    Move-Item $PluginDestino $respaldo
    Write-Ok "Había una versión anterior; se movió a $respaldo"
}
$excluirCarpetas = @(".venv", "__pycache__", "logs", "Intermediate")
if ($Modo -eq "completo") { $excluirCarpetas += "Binaries" }
& robocopy $PluginOrigen $PluginDestino /E /NFL /NDL /NJH /NJS /NP /XD $excluirCarpetas | Out-Null
if ($LASTEXITCODE -ge 8) { Stop-ConError "No se pudo copiar el plugin (robocopy código $LASTEXITCODE)." }
Get-ChildItem $PluginDestino -Recurse -File | Unblock-File
Write-Ok "Plugin copiado"

# --- uv y dependencias de Python ----------------------------------------
Write-Paso "Preparando el servidor MCP (Python)"
$uv = Find-Uv
if (-not $uv) {
    Write-Ok "Instalando uv (gestor de Python de astral.sh)..."
    & powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://astral.sh/uv/install.ps1 | iex"
    $uv = Find-Uv
    if (-not $uv) { Stop-ConError "No se pudo instalar uv. Instálalo a mano desde https://docs.astral.sh/uv/ y vuelve a ejecutar el instalador." }
}
Write-Ok "uv: $uv"
$CarpetaPython = Join-Path $PluginDestino "Python"
& $uv --directory $CarpetaPython sync
if ($LASTEXITCODE -ne 0) { Stop-ConError "Falló la instalación de las dependencias de Python (uv sync)." }
Write-Ok "Dependencias instaladas"

# --- Configurar clientes de IA ------------------------------------------
Write-Paso "Conectando el MCP con tu IA"
$servidor = [ordered]@{
    command = ($uv -replace '\\', '/')
    args = @("--directory", ($CarpetaPython -replace '\\', '/'), "run", "unreal_mcp_server.py")
    env = [ordered]@{ UNREAL_MCP_MODE = $Modo }
}

$configurados = @()

# Claude Desktop (instalador normal y versión de Microsoft Store).
$rutasClaude = @("$env:APPDATA\Claude")
$rutasClaude += Get-ChildItem "$env:LOCALAPPDATA\Packages" -Directory -Filter "Claude_*" -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName "LocalCache\Roaming\Claude" }
foreach ($carpeta in $rutasClaude) {
    if (Test-Path $carpeta) {
        $archivo = Join-Path $carpeta "claude_desktop_config.json"
        if (Set-ServidorMcp $archivo $servidor) { $configurados += "Claude Desktop ($archivo)" }
    }
}

# Claude Code: .mcp.json en la raíz del proyecto.
$mcpJson = Join-Path $CarpetaProyecto ".mcp.json"
if (Set-ServidorMcp $mcpJson $servidor) { $configurados += "Claude Code ($mcpJson)" }

# Cursor: .cursor/mcp.json en el proyecto, solo si Cursor está instalado.
if ((Test-Path "$env:LOCALAPPDATA\Programs\cursor") -or (Test-Path "$env:USERPROFILE\.cursor")) {
    $cursorJson = Join-Path $CarpetaProyecto ".cursor\mcp.json"
    if (Set-ServidorMcp $cursorJson $servidor) { $configurados += "Cursor ($cursorJson)" }
}

foreach ($c in $configurados) { Write-Ok $c }
if (-not ($configurados | Where-Object { $_ -like "Claude Desktop*" })) {
    Write-Aviso "No se encontró Claude Desktop. Si lo instalas después, vuelve a ejecutar este instalador."
}

# --- Listo --------------------------------------------------------------
Write-Host ""
Write-Host "  Listo." -ForegroundColor Green
Write-Host ""
Write-Host "  Siguientes pasos:"
Write-Host "   1. Abre tu proyecto en Unreal. Si pregunta por plugins o por compilar, responde que si."
if ($Modo -eq "completo") {
    Write-Host "      (En modo completo Unreal compila el plugin la primera vez; tarda unos minutos.)"
}
Write-Host "   2. Cierra Claude Desktop por completo (también desde la bandeja, junto al reloj) y ábrelo de nuevo."
Write-Host "   3. Pídele a Claude: 'lista los actores del nivel'. Si responde con la lista, todo funciona."
Write-Host ""
