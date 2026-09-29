[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Profile = 'Release',

    [string] $Destination = 'D:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss',

    [string] $Target,

    [switch] $SkipBuild,
    [switch] $NoPrune,
    [switch] $PreserveConfig
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..')).Path
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FilePath,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]] $Arguments
    )

    Write-Host "==> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath"
    }
}

function Copy-RequiredFile {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $Target
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Missing required artifact: $Source"
    }

    $parent = Split-Path -Parent $Target
    if ($PSCmdlet.ShouldProcess($parent, 'Create directory')) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    if ($PSCmdlet.ShouldProcess($Target, "Copy $Source")) {
        Copy-Item -LiteralPath $Source -Destination $Target -Force
    }
}

function Remove-IfExists {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    if (Test-Path -LiteralPath $Path) {
        if ($PSCmdlet.ShouldProcess($Path, 'Remove unneeded deploy artifact')) {
            Remove-Item -LiteralPath $Path -Recurse -Force
        }
    }
}

$repoRoot = Resolve-RepoRoot
$cargoProfile = if ($Profile -eq 'Release') { 'release' } else { 'dev' }
$profileDir = if ($Profile -eq 'Release') { 'release' } else { 'debug' }
$targetRoot = Join-Path $repoRoot 'target'
$packageRoot = Join-Path $targetRoot 'package'
if ($Target) {
    $targetRoot = Join-Path $targetRoot $Target
    $packageRoot = Join-Path $packageRoot $Target
}
$targetDir = Join-Path $targetRoot $profileDir
$stageDir = Join-Path (Join-Path $packageRoot $profileDir) 'ue4ss'

# xtask builds the core before PakSync and stages the Mod's declared resources.
# Run at the repository root so Cargo loads its .cargo/config.toml as well.
$cargoArgs = @('run', '-p', 'xtask', '--', 'package', '--profile', $cargoProfile, '--mod', 'UE4SSL.PakSync')
if ($Target) {
    $cargoArgs += @('--target', $Target)
}
if ($SkipBuild) {
    $cargoArgs += '--no-build'
}
Push-Location -LiteralPath $repoRoot
try {
    Invoke-Checked -FilePath 'cargo' -Arguments $cargoArgs
} finally {
    Pop-Location
}

$coreDll = Join-Path $targetDir 'UE4SSL.dll'
$pakSyncStage = Join-Path $stageDir 'mods\UE4SSL.PakSync'
$pakSyncDll = Join-Path $pakSyncStage 'main.dll'
$pakSyncConfig = Join-Path $pakSyncStage 'config\paksync.ini'

$ue4ssRoot = $Destination
$gameSpecificRoot = Join-Path $ue4ssRoot 'Deep Rock Galactic'
$workingRoot = if (Test-Path -LiteralPath $gameSpecificRoot -PathType Container) {
    $gameSpecificRoot
} else {
    $ue4ssRoot
}

$modsRoot = Join-Path $workingRoot 'Mods'
$pakSyncModRoot = Join-Path $modsRoot 'UE4SSL.PakSync'

Write-Host "Repo:        $repoRoot"
Write-Host "Profile:     $Profile"
Write-Host "UE4SS root:  $ue4ssRoot"
Write-Host "Working dir: $workingRoot"
Write-Host "Mod dir:     $pakSyncModRoot"

Copy-RequiredFile -Source $coreDll -Target (Join-Path $ue4ssRoot 'UE4SSL.dll')
Copy-RequiredFile -Source $pakSyncDll -Target (Join-Path $pakSyncModRoot 'main.dll')
Copy-RequiredFile -Source (Join-Path $pakSyncStage 'enabled.txt') -Target (Join-Path $pakSyncModRoot 'enabled.txt')

$deployedConfig = Join-Path $pakSyncModRoot 'config\paksync.ini'
if ($PreserveConfig -and (Test-Path -LiteralPath $deployedConfig -PathType Leaf)) {
    Write-Host "Keeping existing config: $deployedConfig"
} else {
    Copy-RequiredFile -Source $pakSyncConfig -Target $deployedConfig
}

if (-not $NoPrune) {
    foreach ($fileName in @(
        'UE4SSL.pdb',
        'UE4SSL.dll.lib',
        'UE4SSL.dll.exp',
        'ue4ssl_javascript.dll',
        'ue4ssl_javascript.pdb',
        'ue4ssl_javascript.dll.lib',
        'ue4ssl_javascript.dll.exp',
        'ue4ssl_lua.dll',
        'ue4ssl_lua.pdb',
        'ue4ssl_lua.dll.lib',
        'ue4ssl_lua.dll.exp'
    )) {
        Remove-IfExists (Join-Path $ue4ssRoot $fileName)
    }

    foreach ($modName in @('UE4SSL.JavaScript', 'UE4SSL.Lua')) {
        Remove-IfExists (Join-Path $modsRoot $modName)
    }

    foreach ($fileName in @('main.pdb', 'main.dll.lib', 'main.dll.exp')) {
        Remove-IfExists (Join-Path $pakSyncModRoot $fileName)
    }
}

Write-Host 'PakSync deployment complete.'
