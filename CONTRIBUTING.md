# Contributing

The original bridge code is licensed under MIT; see LICENSE. Contributions submitted for inclusion are expected to be available under the same license. Do not submit code/assets you cannot legally contribute.

1. Keep game executables/assets, extracted metadata/dumps, saves, Proton prefixes and personal logs out of commits.
2. Preserve the original game UI, fonts and action handlers where possible. Scope native hooks narrowly and validate managed layouts/prologues; unknown builds must fail safely.
3. Never test file moves, clipboard changes or player commands against personal data by default. Use temporary fixtures, an offscreen Qt platform and a private D-Bus session.
4. Do not install/restart desktop environments, KWin or portals to run routine tests. Real suspend/lock/hotplug checks are opt-in manual tests.
5. Run the build/tests and `python3 scripts/audit-source.py`; report the desktop/session, distro, Qt/Proton and game build. Do not present mocks as physical acceptance.
6. Avoid unrelated formatting churn in the current low-level bridge sources. New Python/tooling should be readable and include failure/rollback tests.

See docs/BUILDING.md and docs/ARCHITECTURE.md. Issue templates request minimal redacted diagnostics, not complete game data.
