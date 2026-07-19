param(
    [string[]]$Apps = @("launcher", "retro-core", "prboom-go")
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$WorkspaceRoot = Split-Path $RepoRoot -Parent

$IdfPath = if ($env:FREENOVE_RETRO_IDF_PATH) { $env:FREENOVE_RETRO_IDF_PATH } else { Join-Path $WorkspaceRoot "_esp_idf" }
$IdfToolsPath = if ($env:FREENOVE_RETRO_IDF_TOOLS_PATH) { $env:FREENOVE_RETRO_IDF_TOOLS_PATH } else { Join-Path $WorkspaceRoot "_esp_tools" }
$NinjaPath = Join-Path $IdfToolsPath "tools\ninja\1.12.1\ninja.exe"

if (!(Test-Path $IdfPath)) {
    throw "ESP-IDF not found at '$IdfPath'. Set FREENOVE_RETRO_IDF_PATH."
}
if (!(Test-Path $IdfToolsPath)) {
    throw "ESP-IDF tools not found at '$IdfToolsPath'. Set FREENOVE_RETRO_IDF_TOOLS_PATH."
}

$env:IDF_TOOLS_PATH = $IdfToolsPath
. (Join-Path $IdfPath "export.ps1")

if (Test-Path $NinjaPath) {
    $env:CMAKE_MAKE_PROGRAM = $NinjaPath.Replace("\", "/")
}

$env:IDF_CCACHE_ENABLE = "0"
$env:GIT_CONFIG_COUNT = "1"
$env:GIT_CONFIG_KEY_0 = "safe.directory"
$env:GIT_CONFIG_VALUE_0 = $RepoRoot.Path.Replace("\", "/")

python rg_tool.py --target freenove-s3-qt --no-networking build-img @Apps
