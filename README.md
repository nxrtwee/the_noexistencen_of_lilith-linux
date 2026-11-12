# Lilith Linux Bridge

Unofficial native Linux desktop bridge for **The NOexistenceN of Lilith** — the desktop assistant, **not the visual novel**.

[Русский README](README.ru.md) · [Installation](docs/INSTALLATION.md) · [Compatibility](docs/COMPATIBILITY.md) · [Build](docs/BUILDING.md)

**Status: experimental alpha · KDE Plasma Wayland · x86_64.** This repository does not contain the game and does not replace a Steam license. You need your own installed Steam copy. This is an independent community project, not an official Linux release.

> Source repository: [nxrtwee/the_noexistencen_of_lilith-linux](https://github.com/nxrtwee/the_noexistencen_of_lilith-linux). Binary artifacts remain experimental release candidates; complete the release checklist before publishing a binary prerelease.

## What it does

The game continues to run through Proton. A native Qt/LayerShellQt presenter displays its existing UI above Linux windows and supplies desktop integrations instead of Wine's system UI.

- Native overlay, pointer/keyboard input, window obstacles and conservative motion guards.
- Saved global shortcuts through the desktop portal; physical key positions on RU/EN layouts.
- Native folder picker, safe file/folder/URI opening and Linux-style displayed paths, including Unicode.
- Native tray, menus, notifications and note badge, retaining the original game actions.
- Opt-in XDG autostart, active application/fullscreen context and KDE idle integration.
- Local-file clipboard copy/cut and character-targeted drag/drop.
- External players through MPRIS. The empty local player panel stays hidden during externally requested playback; Stop sends idempotent Pause to the controlled player.
- Recovery of this bridge's KWin observer and portal sessions without restarting the desktop.

No game UI assets, fonts or character artwork are replaced. No GNOME installation is required.

## Requirements

- A **licensed, installed Steam copy** of the desktop-assistant game, app ID `4643090`.
- Linux x86_64 with **KDE Plasma Wayland**. Other desktops, X11 and Flatpak Steam are not validated.
- Steam running and signed in; an existing Proton prefix created by launching the original game at least once.
- A configured Proton executable. DWProton 11.0-16 was physically tested; compatibility with every Proton build is not implied.
- Python 3.11+, Qt 6 Widgets/DBus/Wayland plugins, LayerShellQt, KF6 KIdleTime, X11/Xext compatibility libraries, D-Bus and the KDE portal backend.
- `qdbus6` for scoped observer cleanup; `xdg-mime` and `gio` for the optional default-audio-app fallback.

The initial native binary is dynamically linked on an Arch-family system. It is **not** a universal portable binary; on older distributions build against local libraries.

## Quick start

Keep the bridge folder in a permanent, user-writable location. Close Lilith first. Do not use `sudo`.

For a binary candidate, inspect and install:

```sh
python3 install.py --dry-run --proton /path/to/your/proton
python3 install.py --proton /path/to/your/proton
./launch.sh start
```

If Steam's per-game tool mapping identifies an installed Proton, `--proton` may be omitted. Multiple/undetected installations require explicit paths; see [installation](docs/INSTALLATION.md).

For a source checkout, build first:

```sh
./build.sh
./build-host.sh
ctest --test-dir build/cmake --output-on-failure
python3 install.py --proton /path/to/your/proton
```

Installation copies **only our `version.dll`** into the game directory and registers the native launcher. Existing unrelated files require explicit `--replace-existing` and are backed up. Autostart remains off unless enabled in the game.

**Launch through `Lilith Linux Bridge` in the application menu or `./launch.sh start`, with Steam running. Simply replacing a DLL and pressing Steam's ordinary Play button does not start the native presenter.** Steam launch-option integration is not claimed by this alpha.

```sh
./launch.sh status
./launch.sh stop
python3 install.py --uninstall --dry-run
python3 install.py --uninstall
```

Never run the original launcher and the bridge simultaneously. Do not move/delete the bridge folder after installing without uninstalling/reinstalling its desktop entry.

## Compatibility and limitations

The physically accepted baseline is CachyOS / KDE Plasma Wayland, a single 1920×1080 scale-1 display, Qt 6.12 and the known Steam game build. The game build is hash-checked before installation; a change after configuration blocks startup until reviewed.

- File Ctrl+V requires focus on the character; no global Ctrl+V interception or global keyboard logger.
- Keyboard-only idle is approximated with aggregate system idle.
- The external local panel is intentionally hidden; MPRIS track metadata/progress are not mirrored.
- Portal-overridden bindings may not appear updated in the game's binding display.
- Actual suspend/resume, lock/unlock, monitor hotplug, mixed DPI and long-duration stability need further physical validation.
- Unknown game updates, unseen Open/Save dialogs and other desktop environments are not guaranteed.

See [compatibility](docs/COMPATIBILITY.md) for the verified build and an honest test matrix.

## Privacy and scope

No telemetry, network service or cloud account is added. Clipboard file paths are transferred only for a targeted paste/drop; clipboard text and track metadata are not collected by the bridge. KWin context does not include window titles or process command lines. Local diagnostic logs can still contain filesystem/environment details: **review and redact before attaching them**.

This application is a DLL proxy and in-process hook bridge. Use it only with this game, not with anti-cheat/multiplayer applications. It is not a DRM or Steam entitlement bypass.

## Development and publication

[Architecture](docs/ARCHITECTURE.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [Release checklist](docs/RELEASING.md) · [Changelog](CHANGELOG.md)

The source tree excludes game executables/assets, IL2CPP dumps, saves, prefixes, personal logs and prebuilt binaries. Packaging uses explicit allowlists, not a recursive copy of the working directory. The original bridge code is available under the [MIT License](LICENSE). The game and third-party dependencies are not relicensed; [third-party notices](THIRD_PARTY_NOTICES.md) remain separate.
