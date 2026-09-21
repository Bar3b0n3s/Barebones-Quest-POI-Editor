#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

bash scripts/build-linux.sh Release
./bin-tools/Release/QuestPoiCoreTests

package_name="BarebonesQuestPoiEditor-Linux-x64"
package_root="$project_root/dist/$package_name"
archive_path="$project_root/dist/$package_name.tar.gz"
if [[ -e "$package_root" || -e "$archive_path" ]]; then
    echo "Release output already exists. Move or remove it before packaging again: $package_root" >&2
    exit 1
fi

mkdir -p "$package_root/LICENSES"
cp bin/Release/QuestPoiEditor bin/Release/libstorm.so \
   bin/Release/AtkinsonHyperlegibleNext-Medium.ttf \
   bin/Release/quest-poi-editor-icon.png "$package_root/"
cp packaging/README-Linux.txt "$package_root/README.txt"
cp README.md THIRD_PARTY_NOTICES.md "$package_root/"
cp packaging/barebones-quest-poi-editor.desktop "$package_root/"
cp vendor/imgui/LICENSE.txt "$package_root/LICENSES/Dear-ImGui-MIT.txt"
cp vendor/glfw/LICENSE.md "$package_root/LICENSES/GLFW-zlib.txt"
cp tools/StormLib/LICENSE "$package_root/LICENSES/StormLib-MIT.txt"
cp assets/licenses/STB_IMAGE_LICENSE.txt "$package_root/LICENSES/stb_image-MIT.txt"
cp assets/fonts/OFL.txt "$package_root/LICENSES/Atkinson-Hyperlegible-Next-OFL-1.1.txt"

tar -C "$project_root/dist" -czf "$archive_path" "$package_name"
echo "Created $package_root"
echo "Created $archive_path"
