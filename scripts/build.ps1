param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot

if (-not (Test-Path -LiteralPath 'vendor\imgui\imgui.cpp') -or
    -not (Test-Path -LiteralPath 'vendor\glfw\src\init.c') -or
    -not (Test-Path -LiteralPath 'vendor\stb_image.h') -or
    -not (Test-Path -LiteralPath 'tools\StormLib\StormLib_dll.vcxproj') -or
    -not (Test-Path -LiteralPath 'tools\premake\premake5.exe')) {
    & "$PSScriptRoot\bootstrap.ps1"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio 2022 or newer with Desktop development with C++ is required.'
}
$visualStudio = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $visualStudio) {
    throw 'Visual Studio with the C++ toolchain was not found.'
}
$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\MSBuild.exe'
$major = [int](Split-Path (Split-Path $visualStudio -Parent) -Leaf)
$action = if ($major -ge 18) { 'vs2026' } else { 'vs2022' }
$toolset = if ($major -ge 18) { 'v145' } else { 'v143' }

function Invoke-MSBuild {
    param([string[]] $Arguments)
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $msbuild
    $start.WorkingDirectory = $projectRoot
    $start.UseShellExecute = $false
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }

    # Some launchers provide both Path and PATH. MSBuild's child-process code
    # rejects that environment, so pass a case-insensitively de-duplicated copy.
    $environment = [System.Collections.Generic.Dictionary[string,string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    Get-ChildItem Env: | ForEach-Object { $environment[$_.Name] = $_.Value }
    $start.Environment.Clear()
    foreach ($entry in $environment.GetEnumerator()) { $start.Environment[$entry.Key] = $entry.Value }

    $process = [System.Diagnostics.Process]::Start($start)
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "MSBuild exited with code $($process.ExitCode)." }
}

& '.\tools\premake\premake5.exe' $action
if ($LASTEXITCODE -ne 0) { throw 'Premake generation failed.' }

Invoke-MSBuild -Arguments @('tools\StormLib\StormLib_dll.vcxproj', '/m:1', '/t:Build', "/p:Configuration=Release;Platform=x64;PlatformToolset=$toolset", '/v:minimal')

Invoke-MSBuild -Arguments @('build\BarebonesQuestPoiEditor.slnx', '/m:1', '/t:Build', "/p:Configuration=$Configuration;Platform=x64", '/v:minimal')

$output = Join-Path $projectRoot "bin\$Configuration"
$toolsOutput = Join-Path $projectRoot "bin-tools\$Configuration"
New-Item -ItemType Directory -Force -Path $output, $toolsOutput | Out-Null
Copy-Item -LiteralPath 'assets\fonts\AtkinsonHyperlegibleNext-Medium.ttf' -Destination $output -Force
Copy-Item -LiteralPath 'assets\quest-poi-editor-icon.png' -Destination $output -Force
Copy-Item -LiteralPath 'tools\StormLib\bin\StormLib_dll\x64\Release\StormLib.dll' -Destination $output -Force
Copy-Item -LiteralPath 'tools\StormLib\bin\StormLib_dll\x64\Release\StormLib.dll' -Destination $toolsOutput -Force

$connectorCandidates = @(
    (Join-Path $env:ProgramFiles 'MariaDB\MariaDB Connector C 64-bit\lib\libmariadb.dll'),
    (Join-Path $env:ProgramFiles 'MySQL\MySQL Server 8.0\lib\libmysql.dll'),
    (Join-Path $env:ProgramFiles 'MySQL\MySQL Router 8.0\lib\libmysql.dll')
)
$connector = $connectorCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($connector) {
    Copy-Item -LiteralPath $connector -Destination $output -Force
    Copy-Item -LiteralPath $connector -Destination $toolsOutput -Force
    if ((Split-Path -Leaf $connector) -eq 'libmysql.dll') {
        $mysqlRoot = Split-Path -Parent (Split-Path -Parent $connector)
        foreach ($dependency in 'libssl-3-x64.dll', 'libcrypto-3-x64.dll') {
            $source = Join-Path $mysqlRoot "bin\$dependency"
            if (Test-Path -LiteralPath $source) {
                Copy-Item -LiteralPath $source -Destination $output -Force
                Copy-Item -LiteralPath $source -Destination $toolsOutput -Force
            }
        }
    }
} else {
    Write-Warning 'MariaDB Connector/C was not found. Copy libmariadb.dll beside the editor before connecting to a database.'
}

if ($Configuration -eq 'Release') {
    foreach ($staleArtifact in 'QuestPoiCoreTests.exe', 'QuestPoiCoreTests.pdb',
        'QuestPoiDatabaseIntegrationTests.exe', 'QuestPoiDatabaseIntegrationTests.pdb',
        'QuestPoiEditor.pdb') {
        $stalePath = Join-Path $output $staleArtifact
        if (Test-Path -LiteralPath $stalePath) {
            Remove-Item -LiteralPath $stalePath -Force
        }
    }
}

Write-Host "Built $output\QuestPoiEditor.exe" -ForegroundColor Green
