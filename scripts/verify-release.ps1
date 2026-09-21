param(
    [string] $ReleaseDirectory = 'bin\Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$releasePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $ReleaseDirectory))
if (-not (Test-Path -LiteralPath $releasePath -PathType Container)) {
    throw "Release directory does not exist: $releasePath"
}

$errors = [System.Collections.Generic.List[string]]::new()
foreach ($required in 'QuestPoiEditor.exe', 'StormLib.dll',
    'AtkinsonHyperlegibleNext-Medium.ttf', 'quest-poi-editor-icon.png') {
    if (-not (Test-Path -LiteralPath (Join-Path $releasePath $required) -PathType Leaf)) {
        $errors.Add("Missing required file: $required")
    }
}

$connectors = @('libmariadb.dll', 'libmysql.dll') | Where-Object {
    Test-Path -LiteralPath (Join-Path $releasePath $_) -PathType Leaf
}
if ($connectors.Count -ne 1) {
    $errors.Add('The release must contain exactly one of libmariadb.dll or libmysql.dll.')
}
if ($connectors -contains 'libmysql.dll') {
    foreach ($dependency in 'libssl-3-x64.dll', 'libcrypto-3-x64.dll') {
        if (-not (Test-Path -LiteralPath (Join-Path $releasePath $dependency) -PathType Leaf)) {
            $errors.Add("MySQL runtime dependency is missing: $dependency")
        }
    }
}

$forbidden = Get-ChildItem -LiteralPath $releasePath -File | Where-Object {
    $_.Extension -ieq '.pdb' -or $_.Name -match 'Tests|Extractor'
}
foreach ($file in $forbidden) {
    $errors.Add("Developer-only file is present: $($file.Name)")
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    throw 'Release verification failed.'
}

Write-Host 'Release runtime verification passed.' -ForegroundColor Green
Write-Host 'Runtime files:'
Get-ChildItem -LiteralPath $releasePath -File | Sort-Object Name | ForEach-Object {
    Write-Host "  $($_.Name)"
}
Write-Host 'Add README.md, THIRD_PARTY_NOTICES.md, and the selected connector/dependency licenses when creating the final package.'
Write-Host 'End users need the latest supported x64 Microsoft Visual C++ Redistributable.'
