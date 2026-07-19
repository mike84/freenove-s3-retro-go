param(
    [string[]]$Apps = @("launcher", "retro-core", "prboom-go")
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$WorkspaceRoot = Split-Path $RepoRoot -Parent

function Resolve-DependencyPath {
    param(
        [string]$EnvironmentVariableName,
        [string]$FolderName
    )

    $Override = [Environment]::GetEnvironmentVariable($EnvironmentVariableName)
    if ($Override) {
        return $Override
    }

    $Candidates = @(
        (Join-Path $WorkspaceRoot $FolderName),
        (Join-Path (Join-Path $WorkspaceRoot "HansenLauncher") $FolderName)
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path $Candidate) {
            return $Candidate
        }
    }

    return $Candidates[0]
}

$IdfPath = Resolve-DependencyPath "FREENOVE_RETRO_IDF_PATH" "_esp_idf"
$IdfToolsPath = Resolve-DependencyPath "FREENOVE_RETRO_IDF_TOOLS_PATH" "_esp_tools"
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
