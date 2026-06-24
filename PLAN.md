# APL IME — High-Level Plan

A new APL input method editor based on a fork of mozc. This is the top-level
checklist; detailed task breakdowns follow once the requirements document lands.

## Guiding aim — maintainability & knowledge transfer

**Dyalog maintains this application after the Japanese→APL adaptation, so the goal
is not merely a working system but one Dyalog can maintain independently.** The
human owner must build a good-enough understanding of the codebase as development
proceeds. Accordingly, throughout this plan:

- Explain architecture and rationale as work is done, not just the end result —
  knowledge transfer is a first-class deliverable, not an afterthought.
- Actively surface opportunities for the owner to do hands-on work (manual labour),
  both to deepen understanding and to demonstrate capability.
- Prefer clarity and learnability over cleverness in both plans and code.

## 1. Requirements assessment & prioritisation

- [x] Add requirements document to the project (`docs/requirements.md`).
- [x] Read and break requirements into discrete, testable items.
- [x] Classify each requirement: must-have / should-have / nice-to-have.
- [x] Tag each as reuse-mozc / adapt-mozc / new-build.
- [x] Group requirements into milestones and define MVP scope.
- [x] Record open questions and assumptions for sign-off.

> Output: [`docs/requirements-assessment.md`](docs/requirements-assessment.md).

## 2. Refactor vs. rewrite assessment

- [x] Inventory reusable generic infra vs. Japanese-specific components.
- [x] Decide per-component: keep / adapt / discard / rewrite.
- [x] Decide refactor-existing vs. fresh project using mozc as inspiration.
- [x] Decide whether to track upstream mozc; if so, how (remote, cadence).
- [x] Assess upstream-divergence risk once purpose changes entirely.
- [x] Determine whether the build pipeline itself changes (feeds section 3).
- [x] Record the decision and rationale, then define the migration approach.

> Output: [`REWRITE.md`](REWRITE.md).

## 3. Local build pipeline (for testing)

### Decisions — environments & feedback loop

The build/test loop is **split in two**, because the dev environment (a headless
Linux container on a Windows host) can build and unit-test but cannot run an
interactive IME (no desktop session, no text-services bus):

- **Inner loop — headless, in the dev container.** `bazel test` exercises the
  engine/composer/session logic (the bulk of the APL adaptation). No desktop
  needed; this is the fast everyday loop and mirrors CI's `bazelisk test`.
- **Outer loop — interactive, out of the container.** Actually typing glyphs into
  apps requires a real desktop session + the OS text-services framework, so it
  runs on dedicated test environments per the cadence below.

**Platform cadence:**

- **Linux/ibus — primary, every iteration.** It is **vital that the IME works
  across Wayland-native, XWayland, and X11 applications**, so the main
  interactive-test ground is **two VMs chosen for display-server + distro
  variety**:
  - **Xubuntu** (XFCE / **X11**) — covers native X11 apps.
  - **Fedora GNOME** (**Wayland**) — covers native Wayland apps _and_ XWayland
    apps running under the Wayland session.
    Build _natively in each VM_ (avoids container↔VM library skew and also exercises
    Debian-family vs. Fedora build portability); sync source via git.
- **Windows/TSF — at milestones (parity checks).** Build and test natively on the
  **Windows host** (the developer's own machine, and Dyalog's primary end-user
  platform) at milestone boundaries to confirm parity. Separate clone, native
  MSVC/Bazel build, synced via git push/pull. No Linux→Windows cross-compile
  exists (each platform builds natively — confirmed in `.github/workflows`).
- **macOS — at less-frequent milestones.** Borrow/obtain access to a Mac (ARM) at
  major milestones to validate the InputMethodKit front-end.

> **Deferred platform — fcitx5 (much later).** A Linux fcitx5 front-end is a
> possible _future_ platform (greenfield — absent from the mozc tree; see
> [`REWRITE.md`](REWRITE.md) §2.4 / §6 Q6). It is **explicitly not under
> consideration now** — not part of v1 or the near-term milestones — and is
> recorded here only so it isn't forgotten. To be scoped only much later, if pursued.

> **Build pipeline targets the two Linux VMs (Xubuntu/X11 and Fedora GNOME/Wayland)
> and the Windows host from the outset.** Linux is the day-to-day driver; Windows
> is validated at milestones; macOS least frequently.

**Display-server coverage is a requirement, not a caveat.** Wayland's input-method
story differs from X11's (GTK apps work well with ibus; XWayland apps can behave
differently again), so all three paths — **Wayland-native, XWayland, and X11** —
must be tested deliberately. The Xubuntu + Fedora-GNOME pair covers the matrix.
Note: CI still runs on `ubuntu-24.04` as the canonical _build_ reference; the test
VMs are chosen for runtime display-server/distro variety, not build parity.

### Checklist

- [x] Document toolchain prerequisites (Bazel/bazelisk, platform SDKs) for **both**
      the Linux VM and the Windows host. → [`docs/local_build_and_test.md`](docs/local_build_and_test.md)
- [x] Write a short local build & run guide (`docs/`) covering both environments
      and the inner/outer loop split. → [`docs/local_build_and_test.md`](docs/local_build_and_test.md)
- [~] Get a clean baseline mozc build + tests passing — headless in the container,
      then in each Linux VM, then on the Windows host. **Container: green** —
      `base/...` + `composer/...` build + 55/55 tests pass (Bazel 9.0.2, GCC 12.2).
      VMs + Windows host still pending.
- [ ] Stand up the two Linux VMs (Xubuntu/X11 + Fedora GNOME/Wayland, both with
      ibus) and a Windows-host clone.
- [ ] Verify the IME under all three display-server paths: Wayland-native,
      XWayland, and X11.
- [ ] Apply any build-system changes implied by section 2.
- [ ] Confirm an edit → build → run → test loop works end to end (Linux first).

## 4. GitHub Actions pipeline (versioning + draft releases)

- [ ] Verify/align CI workflows with the build decisions from sections 2–3.
- [ ] Define a versioning scheme and source of truth (`src/version.bzl`).
- [ ] Add automatic version bump / tagging on release-worthy changes.
- [ ] Add automatic creation of **draft releases** with build artifacts attached.
- [ ] Verify per-platform artifacts publish correctly to the draft release.
- [ ] Document the release process.

## 5. Adapt README to explain intention of the fork

- [ ] Link back to mozc project as original work from which this derives, but state the fork will become detached
- [ ] Include some information about current state of APL glyph input, including links to the APL Wiki and Dyalog website
- [ ] Note intentional restrictions, and link to Kanata for a highly configurable cross-platform option
