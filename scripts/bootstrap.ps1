param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot

if (-not (Test-Path -LiteralPath 'vendor\imgui\imgui.cpp')) {
    New-Item -ItemType Directory -Force -Path vendor | Out-Null
    git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git vendor/imgui
}

if (-not (Test-Path -LiteralPath 'vendor\glfw\src\init.c')) {
    New-Item -ItemType Directory -Force -Path vendor | Out-Null
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git vendor/glfw
}

if (-not (Test-Path -LiteralPath 'vendor\stb_image.h')) {
    New-Item -ItemType Directory -Force -Path vendor | Out-Null
    Invoke-WebRequest `
        -Uri 'https://raw.githubusercontent.com/nothings/stb/master/stb_image.h' `
        -OutFile 'vendor\stb_image.h'
}

if (-not (Test-Path -LiteralPath 'tools\StormLib\StormLib_dll.vcxproj')) {
    New-Item -ItemType Directory -Force -Path tools | Out-Null
    git clone --depth 1 https://github.com/ladislav-zezula/StormLib.git tools/StormLib
}

if (-not (Test-Path -LiteralPath 'tools\premake\premake5.exe')) {
    New-Item -ItemType Directory -Force -Path tools\premake | Out-Null
    $archive = 'tools\premake\premake.zip'
    Invoke-WebRequest `
        -Uri 'https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-windows.zip' `
        -OutFile $archive
    Expand-Archive -LiteralPath $archive -DestinationPath tools\premake -Force
}

Write-Host 'Dependencies are ready. Run scripts\build.ps1 next.' -ForegroundColor Green
