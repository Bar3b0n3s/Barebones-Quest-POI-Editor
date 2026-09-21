#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

mkdir -p vendor tools
if [[ ! -f vendor/imgui/imgui.cpp ]]; then
    git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git vendor/imgui
fi
if [[ ! -f vendor/glfw/src/init.c ]]; then
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git vendor/glfw
fi
if [[ ! -f vendor/stb_image.h ]]; then
    curl --fail --location https://raw.githubusercontent.com/nothings/stb/master/stb_image.h --output vendor/stb_image.h
fi
if [[ ! -f tools/StormLib/CMakeLists.txt ]]; then
    git clone --depth 1 https://github.com/ladislav-zezula/StormLib.git tools/StormLib
fi

if ! command -v premake5 >/dev/null 2>&1 && [[ ! -x tools/premake/premake5 ]]; then
    mkdir -p tools/premake
    archive="$(mktemp)"
    curl --fail --location \
        https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-linux.tar.gz \
        --output "$archive"
    tar -xzf "$archive" -C tools/premake
    rm -f "$archive"
fi

echo "Dependencies are ready. Run scripts/build-linux.sh next."
