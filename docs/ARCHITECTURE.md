# Architecture

1. `version.dll` proxies the runtime version library, attaches to the game's IL2CPP domain and installs narrowly scoped hooks. Existing render output is transferred through a bounded shared-memory channel; managed actions stay on the Unity frame thread.
2. `lilith-host` is a Qt Widgets/QRasterWindow + LayerShellQt native presenter. It supplies pointer/text input, native tray/notifications, desktop portals, local file transfer, MPRIS control and KDE idle/session integration.
3. A temporary, bridge-owned KWin script provides window geometry and active-app identity. The receiver validates the actual KWin D-Bus owner. No window captions or process command lines are published.
4. `launch.py` supervises only exact process identities, maintains private session logs/control and shuts down the owned game/presenter. It does not run a broad wineserver/Steam kill.
5. `bridge_config.py` discovers Steam libraries and writes no Steam settings. `install.py` handles explicit backup/install/restore; the config records the accepted game assembly hash.

The shared protocol, bounded request/action rings, stale generations and state transitions are defined in `src/common/`. The launcher layout is generated from the C structures, not maintained as manual offsets. The external Stop wrapper preserves the original method via a signature-guarded trampoline. It does not synthesize global media keys.

No artwork, game assemblies, extracted metadata or Wine/Proton runtime are part of the project. Build artifacts and user state are intentionally separate from Git. KDE/portal/MPRIS interfaces are not claimed to be complete across all desktop implementations.
