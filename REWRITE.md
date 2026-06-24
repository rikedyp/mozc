# Refactor vs. Rewrite Assessment

This document is the work product of **[PLAN.md](PLAN.md) step 2**. It inventories
the upstream **mozc** codebase, decides per component whether to **keep / adapt /
discard / rewrite**, settles the strategic questions (refactor-existing vs. fresh
project, upstream tracking, divergence risk, build-pipeline impact), and defines
the migration approach.

It builds directly on
[`docs/requirements-assessment.md`](docs/requirements-assessment.md) (PLAN.md
step 1), which classified the requirements and gave each a *preliminary* mozc tag
(reuse / adapt / new-build). This document is where those preliminary tags become
**definitive component decisions**, grounded in the actual dependency graph rather
than the surface-level code split in the original planning notes.

Requirement IDs (R1–R38) refer to [`docs/requirements.md`](docs/requirements.md).

---

## TL;DR — the decision

**Refactor in place ("fork-and-prune"), not a fresh rewrite.** mozc is unusually
well-suited to this repurposing: the parts that are expensive to build from
scratch — the three OS text-service integrations (Windows TSF/TIP, macOS
InputMethodKit, Linux ibus), the client/IPC/server process model, the
composition-table engine, the config/protocol/GUI infrastructure, and the Bazel
toolchain — are **engine-agnostic and reusable**. The parts we don't want — the
entire Japanese kana→kanji conversion stack — sit behind a **clean pure-virtual
seam** and can be *deleted*, not untangled.

- **Project-level verdict: ADAPT.** Keep the fork; strip the Japanese stack;
  retarget the generic IME machinery to APL.
- **Component-level "rewrite" is narrow:** it applies to the *engine
  implementation* (replace ~58k LOC of Japanese conversion with a small APL
  key→glyph mapper) and to data/branding — not to the platform or process layers.
- **Upstream: hard-fork.** No scheduled merges from upstream mozc; keep an
  `upstream` remote for reference and cherry-pick only targeted
  platform/security/toolchain fixes. (Endorses requirements-assessment resolved
  decision #3: divergence is unconstrained.)
- **Build pipeline: yes, it changes substantially** — the Japanese data-build
  layer is deleted and mobile is dropped, which feeds PLAN.md §3 and §4.

The headline finding that makes "adapt" the right call is the architectural seam
described next.

---

## 1. The key architectural finding (why "adapt", not "rewrite")

An APL IME is, at its core, a **deterministic keystroke → Unicode-glyph mapper**.
There is no statistical conversion: a glyph is a direct function of a key gesture.
mozc already contains exactly this machinery, already decoupled from the Japanese
statistical engine, in three load-bearing facts confirmed against the code:

1. **The composition engine is a generic, data-driven trie.** `composer::Table`
   is an `input → result / pending` trie loaded from TSV
   (`src/composer/table.cc:463-491`), backed by `Trie<const Entry*>`
   (`src/composer/table.h:147`). Its build target depends only on `base`,
   `protocol`, and abseil — **zero** dependency on converter/dictionary/engine
   (`src/composer/BUILD.bazel`). Multi-key prefix sequences already fall out of
   the trie's `pending` mechanism (the same way `kya` works today with no `k`
   row). The APL prefix-switching model (R6) is therefore largely **data work**.

2. **mozc can already commit straight from the table with no conversion step.**
   The `DIRECT_INPUT` attribute drives `CharChunk::ShouldCommit`
   (`src/composer/char_chunk.cc:507-509`) →
   `Session::CommitStringDirectly` (`src/session/session.cc:~1928`), emitting a
   committed string directly from the composer. This is precisely APL's "key
   gesture → glyph" semantics — the statistical machinery is never on the path.

3. **The Japanese engine sits behind two pure-virtual interfaces.** `Session`
   only ever talks to `engine::EngineInterface`
   (`src/engine/engine_interface.h:49`) and `engine::EngineConverterInterface`
   (`src/engine/engine_converter_interface.h:67`). The **shippable `session`
   library** (`src/session/BUILD.bazel:48-73`) depends only on `//composer`,
   those two `//engine` *interfaces*, and `//transliteration` — **not** on
   converter, dictionary, prediction, rewriter, or the concrete engine. (The
   converter/rewriter/mock deps visible in the same BUILD file are inside
   `session_test`, lines 75–117 — test-only.)

**The stable boundary that protects all platform front-ends is the IPC protocol**
(`commands::KeyEvent` in → `commands::Output` out,
`src/client/client_interface.h:118-142`). win32/tip, mac, and unix/ibus all speak
to the converter over this protocol via `client::ClientInterface`; none of them
depend on the engine internals. Swap what's behind the protocol and the platform
code is undisturbed.

**Caveat — the adapt surface is real but small.** Two source-level couplings must
be edited, not deleted:

- `composer` and `session` use `transliteration::TransliterationType` as their
  *input-mode enum* (`src/composer/composer.h:52,128`, `src/session/session.h:49`,
  real dep at `src/session/BUILD.bazel:67`). This ~190-LOC enum must be **replaced**
  with an APL `InputMode` (e.g. `Apl` / `Ascii`).
- `request::ConversionRequest` includes `prediction/result.h`
  (`src/request/conversion_request.h:50`), which transitively drags
  `dictionary:dictionary_token` in. Slim `ConversionRequest` (or vendor a minimal
  `Result`) so the generic `request` layer stops referencing the discarded stack.

Plus the held-modifier (AltGr) switching model (R5) needs a small piece of **new
C++**: the keymap layer maps a keypress to a *fixed command enum* and cannot emit
arbitrary glyphs (`src/session/keymap.h`, single-key only at
`src/composer/key_parser.cc:201`), so the active modifier must be folded into the
composer lookup token in `Composer::InsertCharacterKeyEvent` /
`CompositionInput::Init`. Prefix-switching (R6) is pure data; mode-toggle (R7,
Stretch) reuses the existing IME on/off state.

---

## 2. Component inventory & disposition

Effort is rough: **S** ≤ a few days, **M** ≈ 1–3 weeks, **L** ≈ 1 month+.

### 2.1 Generic infrastructure — **KEEP**

| Component | Disposition | Rationale | Effort |
|----------|-------------|-----------|--------|
| `src/base/` | **Keep** (prune ~7 files) | Generic cross-platform utilities. Discard `japanese_util.*`, `strings/japanese.*`, `strings/internal/japanese_rules*`; prune kana helpers in `util.h:168-217`, `text_normalizer.h:63-65`. | S |
| `src/ipc/` | **Keep** (rename only) | Generic transport (named pipes / unix sockets / Mach). Only "Japanese" content is branding path strings. | S |
| `src/client/` | **Keep** (rename only) | Pure protobuf relay to the session server. No Japanese logic. | S |
| `src/server/` | **Keep / adapt** | Thin daemon host; swap the engine at the factory. | S |
| `src/storage/` | **Keep generics, discard `louds/`** | Keep `lru_storage`, `encrypted_string_storage`, `existence_filter` (the KV/secret primitives behind R36 config-persistence). `louds/` is used only by the discarded dictionary. | S |
| `src/testing/` | **Keep** | Generic gtest/gmock harness. | — |
| `src/protocol/` | **Keep, trim opportunistically** | `commands.proto` / `config.proto` are the IO currency of the whole pipeline. Conversion-oriented messages simply go unused; don't rewrite. | S (additive) |
| Bazel toolchain (`MODULE.bazel`, `.bazeliskrc`, `bazel/`, generic `build_tools/`) | **Keep** | Modern Bazel 9.0.2 + abseil/protobuf/googletest/Qt/ibus/WiX. See §4. | — |

### 2.2 Reusable IME core — **ADAPT**

| Component | Disposition | Rationale | Effort |
|----------|-------------|-----------|--------|
| `src/composer/` (`table.*`, `composition.*`, `char_chunk.*`) | **Keep core, adapt selection** | The single highest-value asset (§1). Keep the trie/loader as-is; adapt only `Table::InitializeWithRequestAndConfig` (`table.cc:143-316`, Japanese punctuation/table selection). Discard kana transliterators / `mode_switching_handler`. | S–M |
| `src/session/`, `ime_context` | **Adapt** | Keep the key-routing skeleton and the `DIRECT_INPUT` commit path. Strip the **kana-mode conversions** (`ConvertToHiragana`/`CompositionMode*`/full-half-width, `session.h:182-209`) and the Hiragana/Katakana mode states; replace the input-mode enum (§1 caveat). **Retain the candidate-list selection path** (`Convert`/`ConvertNext`/`ConvertPrev`/`CommitCandidate`/`SegmentFocus*`, `session.h:146-176`) — it is the reusable substrate for the R14 keyword search and R17 idiom discovery surfaces (the *engine* behind it is discarded and new-built per §2.5; the selection plumbing + renderer candidate window are kept). For v1's core glyph path the candidate path is simply idle. | M |
| `src/session/keymap` | **Adapt (data)** | Reuse the keymap mechanism for *mode/command* keys (not glyph emission). Rewrite the Japanese keymap TSVs (`data/keymap/*`) to a small APL set. | S |
| `src/config/` + `config.proto` | **Adapt** | `config_handler` depends only on base+proto — trivially extensible. Add `SwitchingModel` + `switching_key` + APL-table fields (precedent: `custom_roman_table`, `config.proto:142`). Ignore/strip kana/dictionary/suggest blocks over time. Drives R8/R9. | S |
| `src/request/` | **Keep proto, slim** | Use `commands::Request`; drop/slim `conversion_request.h` to sever the prediction/dictionary leak (§1 caveat). | S |

### 2.3 Engine seam — **REWRITE (behind a kept interface)**

| Component | Disposition | Rationale | Effort |
|----------|-------------|-----------|--------|
| `src/engine/` interfaces + factory + `minimal_converter` | **Keep** | `EngineInterface` / `EngineConverterInterface` are the clean DI seam; keep the factory pattern. | S |
| `src/engine/` concrete `Engine` internals | **Rewrite (new-build)** | Replace with a small APL `EngineConverterInterface` whose methods are mostly no-ops (APL commits via the composer's `DIRECT_INPUT` path, not via `Convert`). Preferred over reusing `EngineConverter`+`MinimalConverter`, which drags segment/candidate structs. ~50 trivial method bodies. | M |

### 2.4 Platform integration — **KEEP / ADAPT**

| Component | Disposition | Rationale | Effort |
|----------|-------------|-----------|--------|
| `src/unix/ibus/` | **Keep plumbing, adapt data/labels** | Strongest reuse of any platform (~80% language-neutral). Adapt `key_translator.cc` (kana→APL), `property_handler.cc`, `ibus_config.textproto`, `gen_mozc_xml.py` (component id/labels/icon). Serves R18/R20/R25 on the v1 Linux target. | S–M |
| `src/unix/` fcitx5 | **New-build (Stretch)** | Absent from the tree entirely; greenfield when pursued. | — |
| `src/unix/emacs/` | **Discard / defer** | Out of scope; trim from the Linux package. | S |
| `src/win32/tip/` + `win32/base/` | **Adapt core, rewrite profile** | Complete TSF/TIP service (TSF-only, no IMM32). Adapt key pipeline + language bar; **rewrite** `tsf_profile.cc` (new GUIDs, English LANGID 0x0409) and the kana/full-half-width mode model. R11 (Ctrl-switching) is structurally supported — modifiers already forwarded into `KeyEvent`. | M |
| `src/win32/` `broker/`, `cache_service/`, `custom_action/`, `installer/` | **Discard helpers, adapt installer** | Drop mozc process-lifecycle helpers; adapt WiX (new GUIDs/names). | S–M |
| `src/mac/` | **Adapt** | Keep the InputMethodKit shell + `KeyCodeMap`; rewrite the Japanese `ComponentInputModeDict` / mode-ID set in `Info.plist` to a single APL (or APL+ASCII) mode; swap the kana TSV; disable JIS/yen specials. Target = ARM. | M |
| `src/renderer/` | **Keep core, adapt for new UI** | Keep the separate-process model, IPC, and caret positioning (`window_util.cc`). Reusable as-is for a keyword-search list (R14). A 2D popup keyboard map (R15) needs a new window type or a standalone overlay. Both are Should/Stretch. | S (R14) / M (R15) |
| `src/gui/config_dialog/` + `gui/base/` | **Adapt** | Settings shell depends on config/keymap, **not** the discarded stack. `keybinding_editor.*` is the natural basis for the R9 switching-key picker (+ R10 reserved-key confirm). Keep `about_dialog`. | M |
| `src/gui/` `dictionary_tool/`, `word_register_dialog/`, `roman_table_editor`, `character_form_editor`, `post_install_dialog/` | **Discard** | Japanese user-dictionary / kana editors. | S |

### 2.5 Japanese conversion stack — **DISCARD**

Cleanly severable behind the seam in §1. Reverse-dependency checks confirmed no
generic production component depends on these (only test targets, visibility
grants, and the optional dictionary GUI).

| Component | Disposition | Discard scale |
|----------|-------------|---------------|
| `src/converter/` | **Discard** (keep tiny `inner_segment`/`segments` structs if reused) | ~11k LOC |
| `src/dictionary/` | **Discard** (incl. system/user dict + its GUI) | ~5.8k LOC + system data |
| `src/rewriter/` | **Discard** (generic framework optionally keepable, but a deterministic mapper belongs in the composer) | ~17k LOC |
| `src/prediction/` | **Discard** | ~9.8k LOC |
| `src/transliteration/` | **Discard logic; rewrite the exported input-mode enum** (§1 caveat) | ~190 LOC (rewrite) |
| `src/engine/` Japanese internals | **Rewrite** (see §2.3) | ~5k LOC |
| `src/data_manager/` | **Discard** for v1 (no bundled data to serve) | ~3.7k LOC |
| `src/data/` | **Discard** wholesale | ~149 MB / ~16.9M lines |
| `src/storage/louds/` | **Discard** with the dictionary | ~2.9k LOC |

**Total discardable: ≈ 55k–58k C++ LOC + ~149 MB of bundled data.** This is the
bulk of the project's complexity, and it leaves with minimal surgery.

**Future discovery features (R14 keyword search `:reduce`→`/`, R17 idioms):**
**new-build, don't reuse the engines.** The reusable abstractions
(`DictionaryInterface`, `PredictorInterface`, LOUDS trie, `SerializedDictionary`)
are generic in *shape* but every implementation is welded to POS/kana/cost
machinery. A deterministic flat map / small prefix trie (a few hundred lines) is
far simpler. Worth *remembering* (not importing): `base/container/trie.h` and
`SerializedDictionary`'s packed format for a bundled glyph table.

### 2.6 Mobile — **DISCARD / DEFER**

| Component | Disposition | Rationale | Effort |
|----------|-------------|-----------|--------|
| `src/android/`, `src/ios/` | **Discard / defer** | Not in the v1 platform list. Leaf nodes — no desktop target depends on them. Removing them also lets us drop `rules_android`, `rules_go`, `gazelle`, `rules_android_ndk`, and NDK toolchain registration from `MODULE.bazel:127-189`, simplifying every build. | S |

### 2.7 Branding & localisation — **REWRITE / centralise** (R1, R29, R30)

| Surface | Disposition | Notes |
|---------|-------------|-------|
| Build identity | **Centralise** | `config.bzl:38` (`BRANDING`), `:51` (macOS bundle prefix); `base/const.h:35-138` (toggled by `GOOGLE_JAPANESE_INPUT_BUILD` — retire the dual toggle for a single product). |
| Windows | **Rewrite identifiers** | Regenerate the **TSF CLSID** (`base/system_util.cc:463-470`) and profile GUIDs (`win32/base/tsf_profile.cc:42-74`); update `.wxs`, `.rc` display names, DLL output names. |
| macOS | **Rewrite identifiers** | `Info.plist` IMK class/bundle id, `base/mac/mac_util.mm:64-66`, LaunchAgent plist names, `mac/BUILD.bazel` bundle/pkg names. |
| Linux/ibus | **Rewrite identifiers** | `gen_mozc_xml.py` (hard-coded `com.google.IBus.Mozc`, author, product map), icon paths. |
| Localisation (R29/R30) | **Adapt** | Qt `tr()` + per-dialog `*_en.qtts` → `.qm`. Drop `_ja` files; keep English `.qtts` as the documented translator-editable source. English `.qtts`/`.rc`/`.wxs`/plist currently embed "Google Japanese Input" strings — part of the rebrand. |

---

## 3. Strategic decisions

### 3.1 Refactor existing vs. fresh project — **REFACTOR (fork-and-prune)**

**Decision: refactor the existing fork in place.** A fresh project that merely
takes mozc "as inspiration" would have to re-derive the Bazel build, three OS
text-service integrations, the renderer, the IPC/process model, and the composer
trie — i.e. re-build precisely the engine-agnostic assets the fork already has
working. The Japanese stack we *don't* want is the part that detaches cleanly
(§1). The cost/benefit is lopsided in favour of pruning the fork.

The word "rewrite" is reserved for the **engine implementation** (the APL
key→glyph mapper behind the kept interface) and for data/branding. Everything
structural is kept or adapted.

### 3.2 Track upstream mozc? — **NO: hard-fork with reference remote**

**Decision: do not track upstream as a live merge target.** Endorses
requirements-assessment **resolved decision #3** (divergence unconstrained).
Rationale: the product purpose diverges completely; the overwhelming majority of
future upstream commits touch the Japanese conversion stack we are *deleting*, so
scheduled merges would be almost entirely conflicts in code we don't keep, for
near-zero benefit.

**How we relate to upstream instead:**
- **Pin provenance.** This fork derives from upstream mozc commit
  **`988fbca7744f278c74eabe59085be2e255194b19`** (Hiroyuki Komatsu, 2026-06-16,
  "Refactor SystemUtil::GetOSVersionString…") — the last upstream commit before
  the APL work begins on this branch. Baseline version: mozc **3.33.6133.100**
  (`src/version.bzl`). *(Resolves Q10.)*
- **Keep an `upstream` remote for reference only** — never merged on a cadence.
- **Opportunistic, targeted cherry-picks.** Periodically (low cadence, manual)
  scan upstream for fixes in the *subsystems we keep* — `win32/tip`, `unix/ibus`,
  `mac`, `base`, `ipc`, and Bazel/toolchain bumps — and cherry-pick selectively.
  This is "monitor, don't merge."

### 3.3 Upstream-divergence risk — **ACCEPTED (low residual)**

By owner decision #3 the divergence concern (including the prior Wayland-tracking
worry) is removed. **Residual risk:** hard-forking forgoes *automatic* upstream
platform-integration and security fixes. **Mitigation:** the kept subsystems are
mature and slow-moving; the targeted cherry-pick process (§3.2) covers the
high-value cases; and regenerating identifiers (GUIDs, bundle IDs, CLSID) — which
makes future cherry-picks slightly harder — is a one-time, expected cost. Net
risk: **low and accepted.**

### 3.4 Does the build pipeline itself change? — **YES (feeds PLAN.md §3/§4)**

**Decision: substantial build changes are required.**

- **Delete the Japanese data-build layer.** `mozc_dataset` and its ~25 `gen_*`
  genrules (`src/data_manager/mozc_data.bzl`), its two instantiations
  (`data_manager/oss`, `data_manager/testing`), the zip-code genrule
  (`dictionary/BUILD.bazel:747-748`), and the data-codegen `build_tools`
  (`embed_file`, `serialized_string_array_builder`, `tweak_data.py`).
- **Remove external Japanese archives** from `MODULE.bazel` (`ja_usage_dict`,
  `zip_code_ken_all`, `zip_code_jigyosyo`).
- **The package targets break until the APL engine lands.** The hard chain
  `//server → //session:session_server → //engine → //data_manager + //dictionary`
  (`server/BUILD.bazel:107`, `session/BUILD.bazel:97`, `engine/BUILD.bazel:48`)
  means all three desktop `package` targets won't build until the engine/session
  core is replaced — this *is* the central implementation work, and dictates the
  migration sequencing in §4.
- **Drop mobile build deps** (§2.6): `rules_android`, `rules_go`, `gazelle`, NDK.
- **`version.bzl`:** drop `ENGINE_VERSION` / `DATA_VERSION` (meaningless without
  bundled data); keep `MAJOR`/`MINOR`/`BUILD`/`REVISION` as the release source of
  truth for PLAN.md §4.
- **CI:** delete `android.yaml`; keep linux/macos/windows/lint (adapted, renamed
  artifacts); there is **no** release/tag/version automation today — PLAN.md §4
  adds it. The Bazel toolchain itself (9.0.2 + abseil/protobuf/googletest/Qt/
  ibus/WiX) is **kept**.

---

## 4. Migration approach — strangler, keep it green

The guiding principle: **insert the APL seam before deleting Japanese code**, so a
working IME exists at (almost) every step rather than a long red-build period. The
one unavoidable red window is between switching the engine factory and finishing
the APL converter — kept as short as possible.

1. **Baseline & provenance.** Pin the upstream commit (Q10); set up the
   `upstream` reference remote; **get a clean baseline mozc build + tests green
   locally first** (this is PLAN.md §3's first task and the known-good reference
   for everything after). Pick the primary dev/test platform (recommend Linux/ibus
   — strongest reuse, fastest loop; validate macOS-ARM early as a risk, see §5).
2. **Insulate the seam.** Add an APL `EngineInterface`/`EngineConverterInterface`
   implementation (initially passthrough/no-op); replace
   `transliteration::TransliterationType` with an APL `InputMode`; slim
   `ConversionRequest` to drop the prediction/dictionary leak. Build stays green.
3. **Strangle the Japanese stack.** Point the engine factory at the APL engine;
   once nothing references them, delete converter / dictionary / prediction /
   rewriter / transliteration-logic / data / data_manager / storage-louds plus
   their genrules and `MODULE.bazel` archives. Keep the storage generics.
4. **Author APL behaviour as data.** Composer-table TSV(s) for glyph maps; keymap
   TSV(s) for mode/command keys; wire the `SwitchingModel` + `switching_key`
   config fields. Implement the held-modifier token-folding in the composer
   (the one piece of new switching C++).
5. **Adapt session & platform semantics.** Strip kana composition modes from
   `session.cc`; retarget each platform's mode model (win32 `tsf_profile`
   LANGID/GUID + mode manager; mac `ComponentInputModeDict`; ibus
   `property_handler`/`key_translator`).
6. **Rebrand.** Centralise product identity (`config.bzl` + `const.h`);
   regenerate Windows CLSID/GUIDs, macOS bundle ids, ibus component id; drop `_ja`
   translations; retire the `GOOGLE_JAPANESE_INPUT_BUILD` toggle.
7. **Trim build/CI & rebuild tests.** Delete `android/`, `ios/`, `android.yaml`,
   and mobile `MODULE.bazel` deps. Rewrite the session/handler/regression tests
   (which currently depend on the Japanese mocks) against the APL engine — planned
   work, not an afterthought.

This maps onto the requirements-assessment milestones: steps 1–3 unblock **M0
(Foundations)**; steps 4–5 deliver **M1 (Core glyph input)**; the Windows
specifics in step 5 feed **M2 (Windows parity)**.

---

## 5. Risk register

| Risk | Severity | Mitigation |
|------|----------|------------|
| **Windows Japanese LANGID** (`tsf_profile.cc:74`) and full/half-width mode model thread through registration + UI; loading an English-locale APL profile needs care. | High (Win) | Prototype TSF registration with English LANGID + new GUIDs early in M0/M2. |
| **macOS input-mode model** (`ComponentInputModeDict`) is pervasively Japanese and drives input-menu appearance; **ARM is untested** (no arch-specific code, but no ARM evidence). | Med (mac) | Validate a minimal APL `Info.plist` mode set on Apple-Silicon early (step 1/5). |
| **Engine stub breadth** — `EngineConverterInterface` is ~50 methods. | Med | Purpose-built no-op APL converter; commit via the composer `DIRECT_INPUT` path, not `Convert`. |
| **Test rebuild** — session/handler/regression tests depend on Japanese mocks. | Med | Treat as first-class migration work (step 7); rebuild against the APL engine. |
| **Forgoing upstream auto-fixes** (security/platform) after hard-fork. | Low | Targeted cherry-pick process (§3.2); kept subsystems are mature. |
| **R15 popup keyboard map** has the least renderer reuse (list-only drawing). | Low (Stretch) | New window type or standalone overlay; deferred (M7). |

---

## 6. Open questions for sign-off

1. **Session reuse depth (design).** **RESOLVED.** Reuse mozc's session/composer
   wholesale (APL behaviour as data + minimal C++); keep the candidate-list
   selection path as the substrate for R14/R17 (§2.2); strip only the kana-mode
   conversions incrementally. Not investing early in a slimmer session.
2. **Held-modifier implementation locus (design).** **OPEN — revisit before M1.**
   Candidates: fold the active modifier into the composer lookup token
   (recommended) vs. a pre-composer transform. Deferred while build pipelines are
   set up; not needed until the M1 glyph-input work begins.
3. **R11 layout design reference (informs M2).** The existing official Dyalog
   IME maps (e.g. `.din` files) are the **source of information for how the APL
   layouts should be *designed*** — the authoritative reference for which glyphs
   map to which keys, the Ctrl-based conventions, and the international locales
   (R11/R12). They are **not** the code or runtime data source for the new
   project: the IME's key-mapping data is **authored fresh** in the
   composer-table / keymap format, *informed by* the Dyalog layout design — we do
   not import, embed, or mechanically translate the `.din` files into the build.
   A spike to read the existing maps and extract the intended layout design is
   needed during M2.
4. **Engine stub strategy.** **RESOLVED.** Purpose-built no-op `EngineConverter`
   (~50 mostly no-op methods; commit via the composer `DIRECT_INPUT` path, not
   `Convert`), rather than reusing `EngineConverter`+`MinimalConverter`.
5. **R14/R15 UI build (Should/Stretch).** Keyword search reuses the renderer
   candidate list cheaply; the popup keyboard map needs a new surface. Defer
   detailed design; flag as the least-reuse UI items.
6. **fcitx5 (Stretch — much later).** Confirmed greenfield (absent from the
   tree). **Explicitly out of scope for now**; not under consideration during v1
   or the near-term milestones. Recorded here and in PLAN.md as a deliberately
   deferred future platform, to be scoped only much later if/when pursued.
7. **Primary dev/test platform (PLAN.md §3).** **RESOLVED.** Linux/ibus is the
   primary interactive-test target, on **two VMs for display-server variety** —
   Xubuntu (X11) and Fedora GNOME (Wayland + XWayland), every iteration; working
   across Wayland-native/XWayland/X11 is a hard requirement. Windows host is
   validated at milestone parity checks; macOS (ARM) at less-frequent milestones.
   The build pipeline targets both Linux VMs and the Windows host from the outset
   (CI remains `ubuntu-24.04` as the canonical build reference). Headless
   `bazel test` runs in the dev container as the inner loop. See PLAN.md §3
   "Decisions — environments & feedback loop".
8. **`commands.proto`/`config.proto` trimming policy.** **RESOLVED — prune on a
   schedule.** Unused Japanese conversion-oriented messages/fields are removed on
   a defined cadence (not kept indefinitely). Scheduling the prune passes is part
   of the migration/CI planning; until each pass, unused messages may remain but
   are tracked for removal rather than left forever.
9. **User-dictionary scaffolding.** Discard the dictionary GUI for v1 (agreed);
   confirm we are *not* preserving its file-I/O/storage scaffolding for a future
   feature, or note it for later reuse.
10. **Upstream provenance SHA.** **RESOLVED.** Fork derives from upstream commit
    `988fbca7744f278c74eabe59085be2e255194b19` (last upstream commit before the
    APL work on this branch; baseline version 3.33.6133.100). Recorded in §3.2 to
    anchor cherry-picks.

---

## 7. Assumptions

Holding unless the owner says otherwise; consistent with
`docs/requirements-assessment.md`.

- The IPC protocol (`KeyEvent`→`Output`) is the stable boundary; reusing it keeps
  all three platform front-ends working across the engine swap.
- The `DIRECT_INPUT` commit path is sufficient for APL glyph emission, so no
  statistical/candidate machinery is needed on the hot path.
- Discarding the Japanese stack is deletion (behind the seam), not refactoring;
  the only mandatory edits to generic code are the two §1 caveats.
- The existing official Dyalog IME maps are a **design reference** for the APL
  layouts (glyph/key conventions, locales) — not a runtime data or code source.
  All key-mapping data (composer tables / keymaps) is authored fresh in mozc's
  format, informed by that reference; the `.din` files are not imported or
  translated into the project. (See §6 Q3.)
- v1 = MVP scope M0–M5 from `docs/requirements-assessment.md`; mobile, fcitx5,
  and the discovery/popup UI surfaces are out of the critical path.
- Effort sizes (S/M/L) are planning estimates to seed PLAN.md §3, not commitments.
