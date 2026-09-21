Barebones Quest POI Editor - Linux x64
=======================================

Requirements
------------
- A current 64-bit Linux distribution using X11.
- OpenGL 3.0-capable graphics.
- A MariaDB or MySQL client runtime, such as libmariadb.so.3.
- zenity or kdialog for the graphical folder picker. Manual path entry remains
  available when neither is installed.
- A readable World of Warcraft 3.3.5a client.
- Network access and credentials for a TrinityCore 3.3.5a world database.

Running
-------
Keep every file in this folder together, then run:

    ./QuestPoiEditor

The included .desktop file is a packaging template. A distribution package may
install the executable in PATH and the icon in the desktop icon theme.

Safety
------
Back up the world database before editing production data. The editor modifies
only quest_poi and quest_poi_points, and displays a confirmation before saving.

The database password is kept in memory only and is not written to settings.ini.

Licenses
--------
Third-party notices are in THIRD_PARTY_NOTICES.md. Full bundled license texts
are in the LICENSES folder.
