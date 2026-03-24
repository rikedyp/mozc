# Licensing Requirements for Fork Distribution

This document summarises the licence obligations for distributing compiled binaries and installers of the APL IME (a fork of Google's Mozc), whether as GitHub release artifacts, standalone downloads on the Dyalog website, or bundled inside Dyalog installers.

## Permissive Dependencies (notice required)

These dependencies require that copyright notices and licence text are reproduced in documentation or other materials accompanying binary distributions (e.g. a `THIRD_PARTY_NOTICES.txt` file, installer credits screen, or About dialog).

| Dependency | Licence | Notes |
|---|---|---|
| Mozc core | BSD 3-Clause | No endorsement by Google without permission |
| abseil-cpp | Apache 2.0 | Must also include NOTICE file and note modified files |
| protobuf | BSD 3-Clause | No endorsement by Google without permission |
| Material Design Icons | Apache 2.0 | Will be replaced with APL icons, but same licence applies |
| WIL (Windows only) | MIT | Copyright + permission notice |
| googletest | BSD 3-Clause | Test-only — not present in distributed binaries |

The existing `src/data/installer/credits_en.html` bundles all upstream notices. The APL edition should maintain an equivalent file adapted to its actual dependency set.

## LGPL Dependencies (dynamic linking required)

### Qt 6 — LGPL v3 (all platforms)

Qt is the only dependency imposing meaningful constraints on how binaries are distributed. Compliance under LGPL v3 Section 4 (Combined Works) requires all of the following:

1. **Dynamic linking** — Qt must be linked as separate shared libraries (`.dll`/`.so`/`.dylib`), not statically. This is already the case in the Mozc build.
2. **Swappable libraries** — the user must be able to replace the Qt shared libraries with an interface-compatible version. Shipping them as separate files alongside the executable satisfies this — no special relinking tools or object files are needed.
3. **Prominent notice** — state that the product uses Qt under LGPL v3 (in README, release notes, or credits).
4. **Include licence text** — distribute copies of both the LGPL v3 and GPL v3 licence documents with the product.
5. **Link to Qt source** — provide a URL to the Qt source code (e.g. `https://code.qt.io/cgit/qt/qtbase.git/`).
6. **Copyright notice at runtime** — if the application displays copyright notices during execution (e.g. an About dialog), include Qt's copyright among them.

"Installation Information" (LGPL Section 4(e)) only applies to locked-down embedded devices, not standard desktop platforms.

### libibus — LGPL 2.1 (Linux only)

GLib and GObject (transitive dependencies of libibus) are also LGPL 2.1. The same dynamic linking approach as Qt applies.

**Preferred approach for Linux:** declare libibus as a package dependency (in `.deb`/`.rpm` metadata) rather than bundling it. The distro's package manager installs it from its own repos, so Dyalog is not distributing libibus at all — only depending on it. This avoids LGPL distribution obligations entirely and ensures the user receives security updates through normal channels.

If libibus is bundled (e.g. in a standalone tarball or AppImage), ship `libibus.so` as a separate swappable file and include LGPL licence text and a link to source.

## Closed-Source Fork

The APL IME's own source code may remain proprietary provided:

- Qt and libibus are dynamically linked (not statically)
- BSD/Apache/MIT notice requirements are met for all upstream code
- No LGPL-licensed code is copied into or statically linked with proprietary components

The Mozc core (BSD 3-Clause) permits closed-source derivatives. LGPL components permit closed-source use through dynamic linking.

## Bundling Inside Dyalog Installers

No additional obligations beyond the above. Include `THIRD_PARTY_NOTICES.txt` (or equivalent) among the installed files so users can access it post-installation. The notices do not need to be displayed during installation.

## Action Items

1. Create a `THIRD_PARTY_NOTICES.txt` (or adapt `credits_en.html`) covering all runtime dependencies — include in every distributed package.
2. Include full LGPL v3 and GPL v3 licence text files in all distributions.
3. Add a notice in README and release notes: "This software uses Qt under the LGPL v3. Qt source code is available at https://code.qt.io/cgit/qt/qtbase.git/"
4. For Linux packages, declare libibus as a dependency rather than bundling it.
5. Do not switch Qt to static linking on any platform.
