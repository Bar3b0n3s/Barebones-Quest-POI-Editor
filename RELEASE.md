# Release checklist

The build keeps distributable binaries in `bin\Release` and developer tests in `bin-tools\Release`. This document prepares a release but intentionally does not create an archive or installer.

## 1. Build and test

```powershell
.\scripts\build.ps1 -Configuration Release
.\bin-tools\Release\QuestPoiCoreTests.exe
.\bin-tools\Release\QuestPoiDatabaseIntegrationTests.exe
```

The database test reports `skipped` unless its opt-in environment variables are configured. Run it against a disposable test server before publishing a release.

## 2. Verify the runtime directory

```powershell
.\scripts\verify-release.ps1
```

The runtime directory must contain:

- `QuestPoiEditor.exe`
- `StormLib.dll`
- exactly one supported database connector: `libmariadb.dll` or `libmysql.dll`
- connector dependencies reported by the verification script, such as `libssl-3-x64.dll` and `libcrypto-3-x64.dll` for the detected MySQL runtime
- `AtkinsonHyperlegibleNext-Medium.ttf`
- `quest-poi-editor-icon.png`

It must not contain PDBs, test executables, or developer tools.

## 3. Files to add to the final package

- `README.md`
- `THIRD_PARTY_NOTICES.md`
- Any license files required by the database connector and its runtime dependencies

The project owner must choose and add a project license before public source distribution.

For a Linux package, build with `scripts/build-linux.sh`, include `QuestPoiEditor`, `libstorm.so`, the font and PNG icon, declare the MariaDB/MySQL client runtime dependency, and install the launcher template from `packaging/barebones-quest-poi-editor.desktop` with an appropriate absolute executable path or package command.

## 4. End-user prerequisite

Document that users need the latest supported **x64 Microsoft Visual C++ Redistributable**: <https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist>.

## 5. Final manual checks

- Start the editor on a machine without Visual Studio installed.
- Confirm the high-DPI icon appears in Explorer, the taskbar, and the title bar.
- Connect to a test world database and load a quest with one POI and one with multiple POIs.
- Verify add, drag, remove, zoom, pan, undo, redo, save confirmation, and rollback behavior.
- Verify unsaved-change prompts when switching quests, disconnecting, and closing the window.
- Scan the final files with the preferred malware-scanning/signing workflow.
- Create the ZIP or installer manually after these checks pass.
