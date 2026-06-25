# APL IME — Development Plan

A new APL input method editor based on a fork of mozc. **This is the linear,
step-by-step development plan.** It says _what to do and in what order_. Strategic decisions are explained in [`REWRITE.md`](REWRITE.md). The requirement breakdown and
priorities live in [`docs/requirements-assessment.md`](docs/requirements-assessment.md).

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

## How this plan is organised

The plan has two parts:

- **Phase 0 — Foundations (§1–§5).** The preparatory workstreams: the up-front
  analysis (requirements, refactor-vs-rewrite) plus the supporting infrastructure
  (local build pipeline, CI/releases, README). These are established before — and
  then maintained alongside — the migration. Their section numbers are stable
  anchors referenced by the other docs.
- **Migration — linear development sequence.** The actual fork→APL conversion, the
  bulk of the engineering, as an ordered list of steps. Each substantial step
  links to a **work-unit epic** (a sub-plan in [`docs/plans/`](docs/plans/)) that
  is authored when the step starts. A step may in turn spawn per-work-unit PR docs
  in [`docs/prs/`](docs/prs/).

**Why the migration order is what it is — "test before strangle, keep it green."**
The sequence is built so a working IME exists at (almost) every step. APL
behaviour is authored and **validated headlessly** _before_ the irreversible
deletion of the Japanese stack, so the engine/composer logic is proven against
the fast inner loop while working-mozc is still present as a reference.
**Interactive** validation on the Linux VMs comes _after_ the strangle, once the
reduction has shrunk the tree (see the [inner/outer-loop split](#3-local-build-pipeline-for-testing)).
This reconciles the two halves of the feedback loop: headless tests are the
pre-strangle safety net; the VMs are stood up against the leaner post-reduction
codebase.

---

# Phase 0 — Foundations

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
- [x] Determine whether the build pipeline itself changes (feeds §3).
- [x] Record the decision and rationale, then define the migration approach.

> Output: [`REWRITE.md`](REWRITE.md). The migration sequence it used to carry now
> lives below under [Migration](#migration--linear-development-sequence);
> REWRITE.md retains the rationale (architectural finding, component inventory,
> strategic decisions, risks).

## 3. Local build pipeline (for testing)

### Decisions — environments & feedback loop

The build/test loop is **split in two**, because the dev environment (a headless
Linux container on a Windows host) can build and unit-test but cannot run an
interactive IME (no desktop session, no text-services bus):

- **Inner loop — headless, in the dev container.** `bazel test` exercises the
  engine/composer/session logic (the bulk of the APL adaptation). No desktop
  needed; this is the fast everyday loop and mirrors CI's `bazelisk test`. **This
  is the pre-strangle safety net** referenced by the Migration sequence.
- **Outer loop — interactive, out of the container.** Actually typing glyphs into
  apps requires a real desktop session + the OS text-services framework, so it
  runs on dedicated test environments per the cadence below. **Stood up at
  Migration step 5**, against the leaner post-strangle tree.

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
  VMs + Windows host still pending (their interactive bring-up is Migration step 5).
- [ ] **(Migration step 5)** Stand up the two Linux VMs (Xubuntu/X11 + Fedora
      GNOME/Wayland, both with ibus) and a Windows-host clone. Sequenced **after**
      the Japanese-stack strangle (Migration step 4) shrinks the tree — the
      container baseline already evidences portability, so the VMs are better stood
      up against the leaner post-reduction codebase (smaller checkout, fewer deps).
- [ ] **(Migration step 5)** Verify the IME under all three display-server paths:
      Wayland-native, XWayland, and X11.
- [ ] Apply any build-system changes implied by §2 (these land incrementally
      across the Migration steps, not as one batch).
- [ ] Confirm an edit → build → run → test loop works end to end (Linux first).

## 4. GitHub Actions pipeline (versioning + draft releases)

- [ ] Verify/align CI workflows with the build decisions from §2–§3.
- [ ] Define a versioning scheme and source of truth (`src/version.bzl`).
- [ ] Add automatic version bump / tagging on release-worthy changes.
- [ ] Add automatic creation of **draft releases** with build artifacts attached.
- [ ] Verify per-platform artifacts publish correctly to the draft release.
- [ ] Document the release process.

## 5. Adapt README to explain intention of the fork

- [ ] Link back to mozc project as original work from which this derives, but state the fork will become detached
- [ ] Include some information about current state of APL glyph input, including links to the APL Wiki and Dyalog website
- [ ] Note intentional restrictions, and link to Kanata for a highly configurable cross-platform option

---

# Migration — linear development sequence

The actual fork→APL conversion — the bulk of the engineering. This is the
**strangler sequence**: insert the APL seam, prove it, then delete the Japanese
stack, keeping the build green throughout (see
[Why the migration order is what it is](#how-this-plan-is-organised)). The
rationale for each disposition (keep/adapt/discard/rewrite) lives in
[`REWRITE.md`](REWRITE.md) §2; the strategic decisions in §3; the risks in §5.

**Each substantial step links to a work-unit epic** in
[`docs/plans/`](docs/plans/), authored when the step starts. Links to
not-yet-authored epics are expected to be dead until that step begins.

> **Next active unit: Step 2 (Insulate the seam).** Step 1 is done. The migration
> runs strictly in order — even order-free reductions (e.g. mobile removal, a
> leaf-node prune already scoped; see Step 8) are executed in their proper slot,
> not brought forward.

Milestone mapping (from [`docs/requirements-assessment.md`](docs/requirements-assessment.md)):
steps 1–4 unblock **M0 (Foundations)**; steps 3 and 6 deliver **M1 (Core glyph
input)**; the Windows specifics in step 6 feed **M2 (Windows parity)**.

### Step 1 — Baseline & provenance — `[x]`

Pin the upstream commit; set up the `upstream` reference remote; get a clean
baseline mozc build + tests green locally as the known-good reference for
everything after.

- Provenance pinned (`988fbca…`; see [`REWRITE.md`](REWRITE.md) §3.2); container
  baseline green (191/191 tests). Covered operationally by §3.

### Step 2 — Insulate the seam — `[ ]`

Add an APL `EngineInterface`/`EngineConverterInterface` implementation (initially
passthrough/no-op); replace `transliteration::TransliterationType` with an APL
`InputMode`; slim `ConversionRequest` to drop the prediction/dictionary leak. The
build stays green.

> Epic: [`docs/plans/insulate-seam.md`](docs/plans/insulate-seam.md) _(author when started)_.
> Rationale: [`REWRITE.md`](REWRITE.md) §1 (the seam), §2.3 (engine), §1 caveats.

### Step 3 — Author APL behaviour as data — `[ ]`

Composer-table TSV(s) for glyph maps; keymap TSV(s) for mode/command keys; wire
the `SwitchingModel` + `switching_key` config fields. Implement the held-modifier
(AltGr) token-folding in the composer — the one piece of new switching C++.
**Validated headlessly (inner loop)** while working-mozc is still present — this
is the test-before-strangle safety net.

> Epic: [`docs/plans/author-apl-behaviour.md`](docs/plans/author-apl-behaviour.md) _(author when started)_.
> Rationale: [`REWRITE.md`](REWRITE.md) §1, §2.2 (composer/keymap/config), §6 Q2 (held-modifier locus).

### Step 4 — Strangle the Japanese stack — `[ ]`

Point the engine factory at the APL engine; once nothing references them, delete
converter / dictionary / prediction / rewriter / transliteration-logic / data /
data_manager / storage-louds, plus their genrules and `MODULE.bazel` archives.
Keep the storage generics. _(Sequenced after step 3 + headless validation — test
before strangle.)_ This is the reduction that shrinks the tree for step 5.

> Epic: [`docs/plans/strangle-japanese-stack.md`](docs/plans/strangle-japanese-stack.md) _(author when started)_.
> Rationale: [`REWRITE.md`](REWRITE.md) §2.5 (discard list), §3.4 (build-pipeline impact).

### Step 5 — Stand up VMs & interactive validation — `[ ]`

Establish the **outer loop** against the leaner post-strangle tree: stand up the
two Linux VMs (Xubuntu/X11 + Fedora GNOME/Wayland) and the Windows-host clone, and
verify the IME under all three display-server paths (Wayland-native, XWayland,
X11). From here on, the outer loop runs continuously alongside steps 6–8.

> Operational detail: §3 (build pipeline) and
> [`docs/local_build_and_test.md`](docs/local_build_and_test.md). No code epic — this
> is environment bring-up.

### Step 6 — Adapt session & platform semantics — `[ ]`

Strip the kana composition modes from `session.cc`; retarget each platform's mode
model — win32 `tsf_profile` LANGID/GUID + mode manager; mac `ComponentInputModeDict`;
ibus `property_handler`/`key_translator`. Large enough that the epic decomposes
into per-platform PR docs (ibus first — primary target; then win32; then mac).

> Epic: [`docs/plans/platform-semantics.md`](docs/plans/platform-semantics.md) _(author when started)_.
> Rationale: [`REWRITE.md`](REWRITE.md) §2.2 (session), §2.4 (platform integration), §5 (risk register).

### Step 7 — Rebrand — `[ ]`

Centralise product identity (`config.bzl` + `const.h`); regenerate Windows
CLSID/GUIDs, macOS bundle ids, ibus component id; drop `_ja` translations; retire
the `GOOGLE_JAPANESE_INPUT_BUILD` toggle.

> Epic: [`docs/plans/rebrand.md`](docs/plans/rebrand.md) _(author when started)_.
> Rationale: [`REWRITE.md`](REWRITE.md) §2.7 (branding & localisation).

### Step 8 — Trim build/CI & rebuild tests — `[ ]`

Delete `android/`, `ios/`, `android.yaml`, and mobile `MODULE.bazel` deps. Rewrite
the session/handler/regression tests (which currently depend on the Japanese
mocks) against the APL engine — planned work, not an afterthought.

- Mobile (Android/iOS) removal is **already scoped**:
  [`docs/prs/remove-mobile-android-ios.md`](docs/prs/remove-mobile-android-ios.md).
  It is order-free (a leaf-node prune); the `remove-mobile-android-ios` branch is
  reserved for executing it **here**, in its proper slot, to keep the migration
  strictly linear — not brought forward.

> Epic: [`docs/plans/trim-build-ci.md`](docs/plans/trim-build-ci.md) _(author when started)_.
> Rationale: [`REWRITE.md`](REWRITE.md) §2.6 (mobile), §3.4 (CI changes), §5 (test rebuild risk).
