#!/usr/bin/env bash
set -euo pipefail

configuration="${1:-Release}"
case "$configuration" in
    Debug) make_configuration="debug_x64" ;;
    Release) make_configuration="release_x64" ;;
    *) echo "Usage: $0 [Debug|Release]" >&2; exit 2 ;;
esac

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

if [[ ! -f vendor/imgui/imgui.cpp || ! -f vendor/glfw/src/init.c ||
      ! -f vendor/stb_image.h || ! -f tools/StormLib/CMakeLists.txt ]]; then
    bash scripts/bootstrap-linux.sh
fi

premake="$(command -v premake5 || true)"
if [[ -z "$premake" ]]; then
    premake="$project_root/tools/premake/premake5"
fi
if [[ ! -x "$premake" ]]; then
    echo "premake5 is unavailable; run scripts/bootstrap-linux.sh." >&2
    exit 1
fi

"$premake" --os=linux gmake2
make -C build -j"$(nproc)" config="$make_configuration"

cmake -S tools/StormLib -B build/stormlib-linux \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DSTORM_USE_BUNDLED_LIBRARIES=ON
cmake --build build/stormlib-linux --config Release -j"$(nproc)"

output="$project_root/bin/$configuration"
mkdir -p "$output"
storm_library="$(find build/stormlib-linux -type f \( -name 'libstorm.so' -o -name 'libstorm.so.*' \) | head -n 1)"
if [[ -z "$storm_library" ]]; then
    echo "StormLib built successfully, but libstorm.so could not be located." >&2
    exit 1
fi
cp -L "$storm_library" "$output/libstorm.so"
cp assets/fonts/AtkinsonHyperlegibleNext-Medium.ttf "$output/"
cp assets/quest-poi-editor-icon.png "$output/"

echo "Built $output/QuestPoiEditor"
