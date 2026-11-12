# Security and privacy

This project loads a proxy DLL into the user's own game and accepts desktop/clipboard/player data through a native companion. Treat downloaded binaries as executable code; inspect sources and verify archive checksums. Checksums alone do not prove publisher identity.

Do not publish credentials, complete logs, filesystem dumps, game files, saves or Proton prefixes in issues. Before sending logs, redact personal paths, usernames, account identifiers and document names. No telemetry is added by the bridge.

If you find an arbitrary-code execution, unsafe file operation, symlink/ownership bug or data-disclosure issue, do not post a working exploit or personal data publicly. Use GitHub private vulnerability reporting if the maintainer has enabled it. No private contact address is invented in this candidate; the maintainer must establish one before a public release.

Supported security scope initially covers only the latest experimental candidate and its documented KDE setup. Avoid use with anti-cheat, online multiplayer games, untrusted shared binaries or unsupported game builds.
