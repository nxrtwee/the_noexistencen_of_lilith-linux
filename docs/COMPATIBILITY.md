# Compatibility and honest test matrix

## Verified baseline

- Game: The NOexistenceN of Lilith desktop assistant, Steam app 4643090, observed Steam build 26406479.
- GameAssembly.dll SHA-256: `e3c9dffd6fdcece3b6aa7a4f30e9de9235a2630122d04ab0654e532241b6205e`.
- The tested working game assembly and the installed Steam assembly were compared and are byte-identical. Neither is included here.
- Linux x86_64, CachyOS, KDE Plasma Wayland, Qt 6.12, DWProton 11.0-16; one 1920×1080 scale-1 display.
- Shared protocol version 9. A supported hash does not guarantee every runtime/desktop combination.

## Accepted on the development setup

Overlay/input and existing game visuals; window interaction/motion guards; saved hotkeys; native folder selection/opening; Cyrillic paths; tray/menu actions; opt-in autostart; active-app/idle integration; file clipboard/drop; MPRIS launch/control. The external empty-panel/Stop issue was physically retested and accepted after its follow-up fix.

## Isolated automated coverage

Private-bus portal signatures, cancellation and stale replies, shortcut rebinding after mock owner loss, tray generations/callback queues, fake MPRIS control and repeated idempotent Pause, vanished controlled-player isolation, Unicode file batches, newer clipboard protection, temporary autostart/unmanaged-file protection, synthetic recovery, transport (200000 publications), motion/rebound guards. The live bridge-owned KWin observer was removed and automatically restored without restarting KWin.

## Not physically validated

Real suspend/resume and lock/unlock; monitor hotplug, mixed DPI and multiple screens; hours-long resource stability; generic default-audio-app launching on arbitrary desktops; other Proton variants; unknown game builds. GNOME, other desktop environments, X11 and Flatpak Steam have not been validated.

## Deliberate differences

- Wayland file paste requires character focus rather than a global hover-only Ctrl+V hook.
- Keyboard-only idle uses conservative aggregate idle; no global keylogger.
- During externally requested playback the empty local controller is hidden. Track titles/progress/artwork are not mirrored from MPRIS.
- The original music session/menu state is retained; Stop queues Pause to the controlled player. Original local audio cleanup and local playback behavior remain in use.
- Portal trigger overrides may not be reflected in the game settings display.
- The discovered folder/system-opening dialogs were ported; arbitrary unseen Open/Save forms are not covered by a blanket claim.

Game updates can change IL2CPP layouts and machine-code entry points. Keep backups and the startup hash gate. Prologue validation is intentionally narrow; this is not a promise that all hooks are version-independent.
