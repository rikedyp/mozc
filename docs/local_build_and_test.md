# Local build & test guide (APL IME)

This is the APL-IME project's build/test guide. It covers **toolchain
prerequisites** and the **inner/outer loop split** decided in
[`PLAN.md`](../PLAN.md) §3. It complements — and does not replace — the
upstream per-platform build docs:

- Linux: [`build_mozc_for_linux.md`](build_mozc_for_linux.md)
- Windows: [`build_mozc_in_windows.md`](build_mozc_in_windows.md)
- macOS: [`build_mozc_in_osx.md`](build_mozc_in_osx.md)

> **Provenance.** This fork derives from upstream mozc commit
> `988fbca7744f278c74eabe59085be2e255194b19` (baseline version
> `3.33.6133.100`). See [`REWRITE.md`](../REWRITE.md) §3.2.

---

## The two loops

The build/test loop is **split in two** because the dev environment (a headless
Linux container on a Windows host) can build and unit-test, but cannot run an
interactive IME (no desktop session, no text-services bus):

| Loop | Where | What it does | Cadence |
|------|-------|--------------|---------|
| **Inner** | Headless dev container | `bazel test` — engine/composer/session logic (the bulk of the APL adaptation). Mirrors CI's `bazelisk test`. | Every change |
| **Outer** | Interactive VMs + Windows host | Actually typing glyphs into apps; needs a real desktop + OS text-services framework. | Per platform cadence (below) |

**Platform cadence** (see [`PLAN.md`](../PLAN.md) §3 for full rationale):

- **Linux/ibus — primary, every iteration.** Two VMs chosen for display-server
  variety: **Xubuntu (XFCE / X11)** and **Fedora GNOME (Wayland + XWayland)**.
  Working across **Wayland-native, XWayland, and X11** is a hard requirement.
  Build *natively in each VM*; sync source via git.
- **Windows/TSF — at milestones (parity checks).** Build natively on the Windows
  host (MSVC/Bazel). No Linux→Windows cross-compile — each platform builds natively.
- **macOS — at less-frequent milestones.** ARM, to validate the InputMethodKit
  front-end.
- **fcitx5 — deferred (much later).** Not under consideration now; see
  [`REWRITE.md`](../REWRITE.md) §6 Q6.

CI remains `ubuntu-24.04` as the canonical **build** reference; the test VMs are
chosen for runtime display-server/distro variety, not build parity.

---

## Toolchain prerequisites

### Common to all platforms — Bazelisk

[Bazelisk](https://github.com/bazelbuild/bazelisk) wraps Bazel and reads
[`src/.bazeliskrc`](../src/.bazeliskrc) to select the pinned Bazel version
(**9.0.2**). Always invoke `bazelisk` (not a manually-installed `bazel`) — a
Bazel version mismatch is the single biggest source of build failures.

All build/test commands run from the **`src/`** directory.

### Linux (dev container + both VMs)

- **Bazelisk** on `$PATH`.
- **C++ toolchain**: GCC (what CI uses) or Clang. `rules_cc` auto-detects the
  host toolchain; no manual Bazel toolchain config needed. To pin a specific
  compiler: `--repo_env=CC=gcc-14 --repo_env=CXX=g++-14`.
- **Development packages** (Debian/Ubuntu names):
  ```sh
  sudo apt-get update
  sudo apt-get install -y libibus-1.0-dev qt6-base-dev
  ```
  These satisfy the `pkg_config_repository` entries in
  [`src/MODULE.bazel`](../src/MODULE.bazel): `glib-2.0`, `gobject-2.0`,
  `ibus-1.0` (ibus) and `Qt6Core`, `Qt6Gui`, `Qt6Widgets` (qt_linux). On
  Fedora the equivalent packages are `ibus-devel` and `qt6-qtbase-devel`.
- **Skip Android config**: `export ANDROID_NDK_HOME=` (empty) so the build does
  not attempt unnecessary NDK setup. *(Note: mobile/Android is slated for
  removal — see [`REWRITE.md`](../REWRITE.md) §2.6; until then, unset the var.)*

### Windows host (milestone parity)

See [`build_mozc_in_windows.md`](build_mozc_in_windows.md). In short: bazelisk +
Visual Studio C++ toolchain (MSVC) + Qt6; native build, no cross-compile.

### macOS (ARM, milestone)

See [`build_mozc_in_osx.md`](build_mozc_in_osx.md). bazelisk + Xcode toolchain +
Qt6.

---

## Inner loop — headless build & test (dev container / CI)

From `src/`:

```sh
# Build everything that ships in the Linux package.
bazelisk build package --config release_build

# Run the unit-test suite (this is the everyday inner loop).
bazelisk test ... --build_tests_only -c dbg
```

Scoped test runs while iterating:

```sh
# Just the subsystems being changed:
bazelisk test composer/... session/... --build_tests_only -c dbg

# A single test:
bazelisk test base:util_test -c dbg

# Show test output on stderr:
bazelisk test base:util_test --test_arg=--stderrthreshold=0 --test_output=all
```

> ⚠️ Until the APL engine lands, the desktop `package` targets depend on the
> Japanese engine/data chain and **will not build** mid-migration (see
> [`REWRITE.md`](../REWRITE.md) §3.4). During that window, scope builds/tests to
> the subsystems under change rather than the whole `package`.

### Troubleshooting

- **Clean the cache** after toolchain/config changes:
  ```sh
  bazelisk clean --expunge
  ```
- **Linker error `relocation refers to a discarded section`** (GCC 15+ / LLD 19+
  in non-release builds): append `--config no_sframe`.

---

## Outer loop — interactive verification

Runs on the test VMs / Windows host, not the container. Build natively per the
platform doc, install the package, register the IME with the platform text
service (ibus / TSF / IMK), and type glyphs into real applications across all
three Linux display-server paths (Wayland-native, XWayland, X11).

> _Detailed per-VM setup (registering the ibus engine, restarting the bus,
> switching apps under each display server) is filled in once the first Linux VM
> is stood up — PLAN.md §3 checklist._

---

## Status of the baseline (PLAN.md §3)

- [ ] Toolchain prerequisites documented *(this file)*.
- [ ] Clean baseline mozc build + tests green — headless in container.
- [ ] Same, natively in each Linux VM (Xubuntu/X11, Fedora GNOME/Wayland).
- [ ] Same, on the Windows host.
- [ ] IME verified under Wayland-native, XWayland, and X11.
</content>
</invoke>
