OPEN NECTAR — NATIVE PIKMIN PORT FOR macOS (Apple Silicon)

Self-contained package: SDL is included in lib/. Nothing else needs to be
installed. The game runs through macOS's OpenGL 4.1 driver.

This build runs the game's original JAudio sound engine: music, sound
effects and cinematic audio all play.

System requirements:
  - A Mac with Apple Silicon (M1 or later), macOS 12 Monterey or newer.
  - Your legal copy of Pikmin USA Rev. 1 (GPIE01) or Pikmin Europe (GPIP01),
    in ISO or GCM format. RVZ/WIA/GCZ work too if dolphin-tool from a
    Dolphin installation is on PATH or beside the launcher (about 1.4 GB
    of temporary space is needed for the conversion).

The ROM and any Nintendo proprietary resources are not included.
The ROM is not copied or modified during installation.


-------------------------------------------------------------------
1. EXTRACTION AND GATEKEEPER
-------------------------------------------------------------------

Double-click nectar-macos-arm64.zip in Finder, or:

  ditto -x -k nectar-macos-arm64.zip .
  cd nectar-macos

The package is not notarised by Apple, so macOS blocks files that were
downloaded from the internet ("cannot be opened because the developer
cannot be verified"). Clear the download flag once:

  xattr -dr com.apple.quarantine nectar-macos

Keep lib/ next to the executables; they load SDL from there.


-------------------------------------------------------------------
2. INSTALLATION
-------------------------------------------------------------------

From Terminal, give the launcher your disc image and a folder to
install into:

  ./nectar-launcher --rom /path/to/Pikmin.iso --install-dir ~/Games/OpenNectar

This extracts the game data, copies the executables and lib/ into that
folder, and starts the game. The launcher picks nectar or nectar-pal
to match your disc.

To install without starting the game afterwards, add --extract-only.

Double-clicking nectar-launcher in Finder opens it in Terminal, where
the text installer asks for the same two paths.

To play later:

  cd ~/Games/OpenNectar
  ./nectar-launcher

Available options:

  --rom FILE         Pikmin USA Rev. 1 or Europe ISO or GCM image.
  --install-dir DIR  Folder to install to (created if it doesn't exist).
  --extract-only     Install and exit, without launching the game.
  --skip-verify      Skip the integrity checks.
  --dolphin-tool P   Use this dolphin-tool for RVZ/WIA/GCZ images.
  --help             Show help.


-------------------------------------------------------------------
3. SAVES AND SETTINGS
-------------------------------------------------------------------

The memory card is kept in save/ inside the install folder, and
settings in pikmin_settings.conf beside it. In game, F1 opens the
graphics, controls and gameplay settings.


-------------------------------------------------------------------
4. COMMON ISSUES
-------------------------------------------------------------------

"cannot be opened because the developer cannot be verified"
    Run the xattr command from section 1.

"Library not loaded: @executable_path/lib/..."
    lib/ was separated from the executables. Keep the folder intact.

ROM is rejected
    It must be Pikmin USA Rev. 1 (GPIE01, revision 1) or Pikmin Europe
    (GPIP01). Other revisions are not supported.

Insufficient space
    Extracted resources take about 650 MB. Leave at least 1 GB free at
    the destination.
