# Maintainer release checklist

This folder is a release CANDIDATE. Preparing it does not publish a repository, create a release, grant a license or certify compatibility on another machine.

## Before publishing

- [x] The owner approved MIT for the original bridge code; copyright notice names nxrtwee and contributors.
- [ ] Review third-party/game distribution terms and notices; do not include game files, metadata dumps, artwork, fonts, saves or prefixes.
- [ ] Establish a security contact or enable GitHub private vulnerability reporting. No email/address is auto-published.
- [ ] Review README, limitations and support boundaries. Keep unverified conditions marked unverified.
- [ ] Review every staged file and archive manifest; run the privacy/source audit.
- [ ] Build from the cleaned source checkout and run tests.
- [ ] On a separate disposable copy, smoke-test install -> start -> shortcuts/tray/dialogs -> file COPY/drop -> external playback/Stop -> stop -> uninstall. Never test with irreplaceable files.
- [ ] For the first release, verify on a second clean compatible setup if available. Current installer tests are synthetic and do not prove a clean Steam install.
- [ ] Review compiler/runtime provenance and system-library licensing for the exact distributed binary.

## Local checks and artifacts

```sh
./build.sh
./build-host.sh
ctest --test-dir build/cmake --output-on-failure
python3 scripts/audit-source.py
python3 scripts/package-release.py
python3 scripts/audit-source.py --publication
```

The publication audit requires the approved MIT license and a clean source/privacy audit. Passing it does not replace the manual installation and compatibility checks above.

Artifacts in `dist/`: source archive, native Arch-family x86_64 binary candidate, `SHA256SUMS`, and build provenance (no absolute machine paths). Each archive has an internal per-file SHA-256 manifest. These are not signed attestations and not universal Linux binaries. No automatic GitHub publishing workflow exists.

## GitHub preparation (owner actions)

Destination: https://github.com/nxrtwee/the_noexistencen_of_lilith-linux. From this clean folder, review `git status --short` and the source audit. Add only project sources/docs, never build/dist/runtime directories. MIT was approved by the owner before the initial public push. Set the repository description to an unofficial KDE Wayland compatibility bridge for the assistant, not an official native edition.

After review, commit, add the owner's actual repository remote, push `main`, tag the chosen VERSION and create a prerelease. Attach only the reviewed artifacts/checksums. The repository URL and MIT choice were supplied by the owner. No private contact address is published.

Initial tag suggestion: `v0.1.0-alpha.1`. Leave it as a prerelease; do not claim GNOME/X11/Flatpak, mixed DPI or unknown game updates are supported.
