<#
Genera el zip de un release (plugin compilado + servidor Python + instalador) para una versión de Unreal.

  .\Herramientas\empaquetar-release.ps1 -Unreal "C:\Program Files\Epic Games\UE_5.5"

Sube el zip resultante (dist\) a la página Releases de GitHub. Hay que generar un zip por cada
versión de Unreal (5.5, 5.6...): los binarios solo cargan en la versión con la que se compilaron.
#>
param(
    [string]$Unreal = "C:\Program Files\Epic Games\UE_5.5",
    # Ruta corta: BuildPlugin falla con rutas de más de 260 caracteres.
    [string]$Temporal = "$env:SystemDrive\umm-build"
)

$ErrorActionPreference = "Stop"
$Raiz = Split-Path -Parent $PSScriptRoot
$Plugin = Join-Path $Raiz "UnrealMCP"
$RunUat = Join-Path $Unreal "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path $RunUat)) { throw "No se encontró RunUAT.bat en $Unreal" }

$uplugin = [System.IO.File]::ReadAllText((Join-Path $Plugin "UnrealMCP.uplugin")) | ConvertFrom-Json
$versionMotor = (Get-Content (Join-Path $Unreal "Engine\Build\Build.version") -Raw | ConvertFrom-Json)
$ue = "$($versionMotor.MajorVersion).$($versionMotor.MinorVersion)"
$nombre = "unreal-mcp-max-v$($uplugin.VersionName)-UE$ue-Win64"

if (Test-Path $Temporal) { Remove-Item $Temporal -Recurse -Force }
$paquete = Join-Path $Temporal "UnrealMCP"

& $RunUat BuildPlugin "-Plugin=$Plugin\UnrealMCP.uplugin" "-Package=$paquete" -TargetPlatforms=Win64 -Rocket
if ($LASTEXITCODE -ne 0) { throw "BuildPlugin falló (código $LASTEXITCODE)" }
# BuildPlugin deja este archivo en el plugin de origen.
Remove-Item (Join-Path $Plugin "Config\FilterPlugin.ini") -ErrorAction SilentlyContinue
if (-not (Get-ChildItem (Join-Path $Plugin "Config") -ErrorAction SilentlyContinue)) {
    Remove-Item (Join-Path $Plugin "Config") -ErrorAction SilentlyContinue
}

# El servidor Python no es parte del build de Unreal: se copia aparte.
& robocopy (Join-Path $Plugin "Python") (Join-Path $paquete "Python") /E /NFL /NDL /NJH /NJS /NP /XD .venv __pycache__ logs *.egg-info | Out-Null
if ($LASTEXITCODE -ge 8) { throw "No se pudo copiar Python (robocopy $LASTEXITCODE)" }
Remove-Item (Join-Path $paquete "Intermediate") -Recurse -Force -ErrorAction SilentlyContinue

$contenido = Join-Path $Temporal "zip\$nombre"
New-Item -ItemType Directory -Force $contenido | Out-Null
Move-Item $paquete (Join-Path $contenido "UnrealMCP")
Copy-Item (Join-Path $Raiz "Instalador") $contenido -Recurse
foreach ($archivo in "Instalar.bat", "Instalar-Completo.bat", "README.md", "LICENSE") {
    Copy-Item (Join-Path $Raiz $archivo) $contenido
}

$dist = Join-Path $Raiz "dist"
New-Item -ItemType Directory -Force $dist | Out-Null
$zip = Join-Path $dist "$nombre.zip"
if (Test-Path $zip) { Remove-Item $zip }
# Compress-Archive de PowerShell 5.1 escribe rutas con "\" y otros descompresores no las entienden.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$base = Split-Path -Parent $contenido
$archivoZip = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($archivo in Get-ChildItem $contenido -Recurse -File) {
        $entrada = $archivo.FullName.Substring($base.Length + 1) -replace '\\', '/'
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archivoZip, $archivo.FullName, $entrada, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $archivoZip.Dispose()
}
Remove-Item $Temporal -Recurse -Force
Write-Host "Release listo: $zip" -ForegroundColor Green
