# Building and tests

The bridge has a Linux native host and a Windows x86_64 DLL proxy. The game is not needed for source compilation or isolated tests. Do not add proprietary game files to the checkout.

## Toolchain

- CMake 3.20+, C/C++17 compiler, Python 3.11+, pkg-config.
- Qt 6.6+ Widgets/DBus development packages and moc, KDE LayerShellQt and KF6IdleTime development packages, X11/Xext headers, D-Bus test runner.
- Clang with Windows GNU target support, LLD, Wine development Windows/MSVCRT headers and import libraries including ucrtbase. The tested build uses this combination, not a bundled Windows SDK.

```sh
./build.sh
./build-host.sh
ctest --test-dir build/cmake --output-on-failure
python3 scripts/audit-source.py
```

`build.sh` discovers common Wine import-library locations. Override `LILITH_WINE_INCLUDE`, `LILITH_WIN_LIBDIR` and `CLANG` when required. The native build uses CMake imported dependency targets, not a local compiler-version path. Standard `CC`, `CXX`, `CMAKE_PREFIX_PATH` and `JOBS` overrides are supported. Do not assume the binary built on a rolling distro will run on an older system.

Outputs: `build/version.dll`, `build/lilith-host`, derived `build/shm-layout.json`. Always deploy a matching host/proxy/protocol layout. The runtime also needs `src/host/kwin-windows.js` at its packaged location.

## Test scope

CTest runs three C motion/transport guards, four offscreen/private-session-bus integration mocks and the temporary-root Python installer tests. Assertions remain enabled in release-built tests. Mock tests do not read the desktop clipboard, control real players, change real autostart or restart the desktop. Installer fixtures are not game binaries and never launch the game.

`tests/music-stop-trampoline-test.c` is an optional separate Wine test for the guarded Stop trampoline. It runs synthetic executable code, not the game. Build with the same Windows-target headers/import libraries and run in a disposable prefix; do not claim that it is an end-to-end player test.

Actual overlay rendering/interaction, file operations, external player control, sleep/unlock, hardware hotplug, mixed DPI and resource stability need explicit manual acceptance. The native CMake job is available as a manually triggered Arch Linux workflow; the default lightweight workflow tests Python/tooling and pure C guards without installing a desktop environment.

## Packaging

```sh
python3 scripts/package-release.py
```

This creates source and Arch-family x86_64 binary candidate archives with per-file manifests and archive SHA-256 checksums. Artifacts contain only allowlisted project files and our own host/proxy, not game files, dumps or a Proton runtime. See docs/RELEASING.md for publication and manual acceptance checks.
