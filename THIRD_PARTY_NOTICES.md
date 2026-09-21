# Third-party notices

Barebones Quest POI Editor uses the following third-party components. Include the applicable upstream license files when distributing binaries.

## Dear ImGui

Copyright (c) 2014-2026 Omar Cornut. Licensed under the MIT License. The full license is available in `vendor/imgui/LICENSE.txt` and at <https://github.com/ocornut/imgui>.

## GLFW

Copyright (c) 2002-2006 Marcus Geelnard and copyright (c) 2006-2024 Camilla Löwy and contributors. Licensed under the zlib/libpng license. The full license is available in `vendor/glfw/LICENSE.md` and at <https://github.com/glfw/glfw>.

## stb_image

`stb_image.h` is by Sean Barrett and contributors and is available under the MIT License or the public domain dedication. The source and license terms are available at <https://github.com/nothings/stb>.

## Atkinson Hyperlegible Next

Atkinson Hyperlegible Next is provided by the Braille Institute of America under the SIL Open Font License 1.1. The included license is `assets/fonts/OFL.txt`; the official font project is at <https://github.com/googlefonts/atkinson-hyperlegible-next>.

## StormLib

Copyright (c) 1999-2013 Ladislav Zezula. Licensed under the MIT License. The full license is available in `tools/StormLib/LICENSE` and at <https://github.com/ladislav-zezula/StormLib>.

## Database connector

The runtime uses either MariaDB Connector/C or MySQL's client library, selected according to what is installed when the release is built. Their licenses are not interchangeable. Distributors must include and comply with the license belonging to the exact connector DLL placed in the release.

- MariaDB Connector/C: <https://mariadb.com/kb/en/mariadb-connector-c/>
- MySQL Community downloads and licensing information: <https://www.mysql.com/about/legal/licensing/>

If the selected connector brings additional runtime libraries such as OpenSSL, include their required license and notice files as well.

## Application icon

The blue, gold, and silver quest-star application icon is original generated artwork created for this project. It replaces the earlier experimental crop from game client artwork; no BLP-derived icon remains in the project.
