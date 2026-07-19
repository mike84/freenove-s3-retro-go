param(
    [string]$Port = "COM3",
    [int]$Baud = 921600
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

if (!(Test-Path $IdfPath)) {
    throw "ESP-IDF not found at '$IdfPath'. Set FREENOVE_RETRO_IDF_PATH."
}
if (!(Test-Path $IdfToolsPath)) {
    throw "ESP-IDF tools not found at '$IdfToolsPath'. Set FREENOVE_RETRO_IDF_TOOLS_PATH."
}

$env:IDF_TOOLS_PATH = $IdfToolsPath
. (Join-Path $IdfPath "export.ps1")

$Image = Get-ChildItem -Path $RepoRoot -Filter "retro-go_*_freenove-s3-qt.img" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$Image) {
    throw "No Freenove Retro-Go image found. Run .\scripts\build.ps1 first."
}

python -m esptool --chip esp32s3 -p $Port -b $Baud --before default_reset --after hard_reset write_flash --flash_size detect 0x0 $Image.FullName
