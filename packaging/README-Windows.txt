Barebones Quest POI Editor - Windows x64
=========================================

Requirements
------------
- Windows 10 or newer, 64-bit.
- OpenGL 3.0-capable graphics.
- The latest Microsoft Visual C++ x64 Redistributable.
- A readable World of Warcraft 3.3.5a client.
- Network access and credentials for a TrinityCore 3.3.5a world database.

Running
-------
Keep every file in this folder together and run QuestPoiEditor.exe.

On first use, open Settings, select the WoW client folder, enter the locale and
world database connection details, then choose Connect.

Safety
------
Back up the world database before editing production data. The editor modifies
only quest_poi and quest_poi_points, and displays a confirmation before saving.

The database password is kept in memory only and is not written to settings.ini.

Licenses
--------
Third-party notices are in THIRD_PARTY_NOTICES.md. Full bundled license texts
are in the LICENSES folder.
