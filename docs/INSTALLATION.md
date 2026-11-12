# Installation, configuration and removal

Use a permanent folder. Close the game before replacing its DLL. Never install as root. You need your own Steam copy of the desktop assistant (app 4643090), a running Steam client and an existing Proton prefix created by a normal first launch.

## Native dependencies

Arch-family runtime package names commonly include `python`, `qt6-base`, `qt6-wayland`, `layer-shell-qt`, `kidletime`, `qt6-tools`, `xdg-desktop-portal`, `xdg-desktop-portal-kde`, `dbus`, `libx11`, `libxext`, `xdg-utils` and `glib2`. Package names/versions differ elsewhere; no installer runs a package manager or installs a desktop environment. `gio` is used only for optional system-default audio-app launching.

The host needs a working Qt Wayland layer-shell plugin. `qdbus6` is used to unload only the bridge's observer after exit. Binary candidates are linked against the build machine's Qt/KDE/glibc/libstdc++; use a local source build when those versions are not compatible.

## Inspect then install

```sh
python3 install.py --dry-run --proton /path/to/proton
python3 install.py --proton /path/to/proton
```

Automatic discovery reads standard Steam roots, libraryfolders.vdf and appmanifest_4643090.acf. If there is ambiguity, supply paths:

```sh
python3 install.py --dry-run \
  --game-dir '/path/to/SteamLibrary/steamapps/common/The NOexistenceN of Lilith' \
  --steam-root '/path/to/Steam' \
  --compat-data '/path/to/SteamLibrary/steamapps/compatdata/4643090' \
  --proton '/path/to/compatibility-tool/proton'
```

Remove `--dry-run` after reviewing the plan. A known build hash is required unless `--accept-untested-game` is explicitly supplied. That option accepts compatibility risks; it is not a guarantee and does not remove the check for subsequent changes to that configured build.

If an unmanaged `version.dll` or desktop entry exists, installation stops. Use `--replace-existing` only after checking for other mods/launchers. The installer stores per-user backups and restores them on uninstall. Updates of an owned installation keep the original pre-bridge backup. Symlink replacement is refused. GameAssembly.dll, saves, resources and the Proton prefix are never replaced by the installer.

## Launch

```sh
./launch.sh start
./launch.sh status
./launch.sh stop
```

Use the application-menu entry or the supervisor, not the original game launcher simultaneously. Ordinary Steam Play is not wired to start the native presenter by this alpha. Moving the bridge folder invalidates the registered launcher; uninstall/reinstall when relocating.

Autostart is controlled by the original setting in Lilith. It creates a managed XDG autostart Desktop Entry pointing to the supervisor. Installation alone never enables it.

## Local state

- Config: `$XDG_CONFIG_HOME/lilith-linux/config.json` (default `~/.config`).
- Install record, backups, logs: `$XDG_STATE_HOME/lilith-linux/` (default `~/.local/state`).
- Launcher: `$XDG_DATA_HOME/applications/org.lilith.LinuxBridge.desktop` (default `~/.local/share`).
- Autostart, only if opted in: `$XDG_CONFIG_HOME/autostart/org.lilith.LinuxBridge.desktop`.
- Session control: `$XDG_RUNTIME_DIR/lilith-linux/`.
- Frame transport: `/dev/shm/lilith-linux-frame`, restricted to its user.
- The proxy also writes `lilith_linux.log` next to the game executable. Review this log before sharing it.

For fixture tests, `LILITH_CONFIG` can override the config filename. Never point tests at real user configuration.

## Uninstall / rollback

```sh
./launch.sh stop
python3 install.py --uninstall --dry-run
python3 install.py --uninstall
```

Uninstall restores original backed-up files, or removes only unchanged installed files where no originals existed. It refuses modified files/missing or altered backups. An autostart entry is removed only when managed and explicitly referring to this bridge folder. Configuration and backups are retained; remove them manually only after review. The original game, saves and prefix remain. Steam file verification may be used as a separate user action, not as an automated installer step.

## Troubleshooting

- Unknown hash after a Steam update: do not force it blindly. Stop, check compatibility, back up and reconfigure only after review.
- Missing Qt/KDE libraries or symbol versions: build locally; no GNOME install is needed.
- Portal prompt/shortcut failure: check the KDE portal backend and application identity. Do not repeatedly reset global portal settings or restart the desktop as a default fix.
- Another bridge owns the session: close it using its own launcher before switching installations.
- Cyrillic paths: the supervisor scopes UTF-8 locale to this application; do not remove that setting or change the system locale as a workaround.
- File paste: focus the character, then Ctrl+V. Test moves only on disposable copies.
