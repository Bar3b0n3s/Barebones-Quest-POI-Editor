param(
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot

& "$PSScriptRoot\build.ps1" -Configuration $Configuration
& "$PSScriptRoot\verify-release.ps1" -ReleaseDirectory "bin\$Configuration"

$packageName = 'BarebonesQuestPoiEditor-Windows-x64'
$packageRoot = Join-Path $projectRoot "dist\$packageName"
$archivePath = Join-Path $projectRoot "dist\$packageName.zip"
if ((Test-Path -LiteralPath $packageRoot) -or (Test-Path -LiteralPath $archivePath)) {
    throw "Release output already exists. Move or remove it before packaging again: $packageRoot"
}

$licenses = Join-Path $packageRoot 'LICENSES'
$pictures = Join-Path $packageRoot 'assets\pictures'
New-Item -ItemType Directory -Force -Path $licenses, $pictures | Out-Null
foreach ($file in 'QuestPoiEditor.exe', 'StormLib.dll', 'AtkinsonHyperlegibleNext-Medium.ttf',
    'quest-poi-editor-icon.png', 'libmysql.dll', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll') {
    Copy-Item -LiteralPath (Join-Path $projectRoot "bin\$Configuration\$file") -Destination $packageRoot
}
Copy-Item -LiteralPath 'README.md', 'THIRD_PARTY_NOTICES.md' -Destination $packageRoot
Copy-Item -LiteralPath 'assets\pictures\App.png' -Destination $pictures
Copy-Item -LiteralPath 'vendor\imgui\LICENSE.txt' -Destination (Join-Path $licenses 'Dear-ImGui-MIT.txt')
Copy-Item -LiteralPath 'vendor\glfw\LICENSE.md' -Destination (Join-Path $licenses 'GLFW-zlib.txt')
Copy-Item -LiteralPath 'tools\StormLib\LICENSE' -Destination (Join-Path $licenses 'StormLib-MIT.txt')
Copy-Item -LiteralPath 'assets\licenses\STB_IMAGE_LICENSE.txt' -Destination (Join-Path $licenses 'stb_image-MIT.txt')
Copy-Item -LiteralPath 'assets\fonts\OFL.txt' -Destination (Join-Path $licenses 'Atkinson-Hyperlegible-Next-OFL-1.1.txt')

$mysqlLicense = Join-Path $env:ProgramFiles 'MySQL\MySQL Server 8.0\LICENSE'
if (-not (Test-Path -LiteralPath $mysqlLicense)) {
    throw 'The MySQL license matching the bundled connector could not be found.'
}
Copy-Item -LiteralPath $mysqlLicense -Destination (Join-Path $licenses 'MySQL-8.0-and-bundled-components.txt')

Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
Write-Host "Created $packageRoot" -ForegroundColor Green
Write-Host "Created $archivePath" -ForegroundColor Green
