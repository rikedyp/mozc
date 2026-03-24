Mozc APL IME — Development Plan
================================

Executive summary of the progression from the existing Mozc Japanese IME
codebase to a cross-platform Dyalog APL glyph input method editor.

---

## Current State

Mozc is a mature, cross-platform Japanese IME with a client/server
architecture running on Linux (IBus), macOS (IMK), and Windows (TSF).
Three proof-of-concept branches demonstrate APL shifting-key input:

- **`3-shifting-keys`** (Linux/IBus) — most complete: 98 glyphs mapped,
  7 configurable modifiers, menu UI, mode persistence. Known issues with
  layout inconsistency (Alt path uses keyvals not scancodes) and L/R Ctrl
  independence.
- **`apl-shifting-macos`** (macOS/IMK) — working POC: ~80 glyphs, Ctrl
  and Option as shifting keys, virtual-key-based layout independence,
  APL composition mode wired through session layer.
- **`windows-poc`** (Windows/TSF) — partial: English registration and
  language bar menu UI complete, but config persistence (W4) and glyph
  production (W5) not yet implemented.

### Component Reusability Assessment

| Component | Verdict | Rationale |
|-----------|---------|-----------|
| **Platform clients** (IBus, IMK, TSF) | **Keep & adapt** | Mature OS integration; PoC branches prove they work for APL key interception |
| **Session / keymap layer** | **Keep & adapt** | Mode state machine is reusable; replace Japanese modes with APL modes |
| **Composer (Trie/preedit tables)** | **Adapt for prefix/dead-key & overstrike** | TSV table loader and Trie lookup are language-agnostic; replace Japanese tables with APL composition rules |
| **Converter internals** (lattice, Viterbi, connector, segmenter, NBest) | **Delete** | Deeply coupled to Japanese morphology (POS-based transition costs, morpheme boundaries); no future APL use whatsoever |
| **Converter shell** (`ConverterInterface`, `EngineConverterInterface`, `EngineConverter`) | **Keep & hollow out** | Session calls converter ~30 times; predictor is accessed *through* the converter. Keep as thin routing layer: `Convert()` becomes no-op, `Suggest()`/`Predict()` route to predictors. Load-bearing for Phase 4. |
| **Predictor interface** | **Reuse with new implementation** | `PredictorInterface` is a clean, minimal abstraction; implement APL keyword/idiom predictors behind same API |
| **User history predictor** | **Reuse as-is** | Frequency-based ranking of previously selected glyphs is useful for APL too |
| **Rewriter** | **Delete** | Japanese-specific post-processing (collocation, calculator, symbols); no APL equivalent needed |
| **Renderer** | **Keep & simplify** | Candidate window still needed for keyword search and idiom completion; strip Japanese-specific rendering |
| **Config dialog (Qt)** | **Keep & rebrand** | Replace Japanese options with APL settings (shifting key selection, locale, input modes) |
| **Build system (Bazel)** | **Keep** | Platform-agnostic; only MODULE.bazel dependency list changes |

---

## Phase 0 — Foundation & Cleanup

**Goal**: Merge PoC learnings, strip Japanese-specific code and data,
establish the APL fork as a clean starting point.

### 0.1 Redesign protocol enums
The protocol buffers (`commands.proto`, `config.proto`) define enums
whose numeric values are assumed throughout the codebase — 474+
references to `CompositionMode` values alone across 30+ files, plus
downstream use in `transliteration.h` (19 references),
`keymap.cc` (28 references), platform renderers, and iOS/Android
bridges. The three PoC branches took incompatible approaches: Linux
appended `APL = 6` keeping all Japanese values; Windows repurposed
`FULL_KATAKANA = 2` as `APL = 2` and deleted `HALF_KATAKANA`.

This step defines the authoritative APL protocol *before* merging or
deleting code, because every subsequent step depends on it:

- **Audit all protocol enums** in `commands.proto` and `config.proto`
  for Japanese-specific values: `CompositionMode`, `SpecialKey`,
  `SessionCommand`, `KeyMapEntry` state enums, `Config` fields, etc.
- **Design the APL enum set**: determine which values to keep (DIRECT
  is essential), which to remove (HIRAGANA, KATAKANA variants,
  HALF_ASCII, FULL_ASCII), and which to add (APL, potentially
  APL_TOGGLE, APL_PREFIX for Phase 2 modes).
- **Trace every reference** to removed/renumbered enum values across
  the codebase — session, keymap, transliteration, composer,
  platform clients (IBus, IMK, TSF), renderer, config dialog, and
  tests. Produce a reference map so that subsequent deletion in 0.3
  and platform work in Phase 1 can proceed safely.
- **Decide on wire compatibility**: since this is a fork (not a
  rolling update to deployed Mozc), proto field numbers and enum
  values can be freely renumbered. There is no need to preserve
  backward compatibility with Japanese Mozc serialized configs or
  IPC messages.

### 0.2 Merge and reconcile PoC branches
Unify the three PoC branches onto a single development branch using
the protocol design from 0.1 as the source of truth. The branches
share session-layer wiring (`apl_keymap.h/cc`, session APL mode) but
diverged in platform-specific code. Reconcile conflicts, adopt the
best patterns from each:
- Linux: scancode-based glyph lookup from `mozc_engine.cc`
- macOS: virtual-key-based lookup from `apl_keycode_map.h`
- Windows: `GetKeyState(VK_*)` left/right distinction pattern

### 0.3 Remove Japanese-specific data and code
- Delete Japanese dictionary data (including IPAdic / NAIST dictionary
  and Tamachi Phonetic Kanji), usage dictionary, and zip code data from
  the build and `MODULE.bazel`
- Remove romaji-to-kana preedit tables from `src/data/preedit/`
- Remove Japanese keymap tables from `src/data/keymap/`
- Strip Japanese input modes (Hiragana, Katakana, Half-width, etc.) from
  session, keymap, menus, and protocol enums
- Remove the rewriter module (Japanese collocation, calculator, symbol
  insertion)
- **Aggressively delete the converter internals**: remove the lattice,
  Viterbi search, connector (POS transition costs), segmenter
  (morpheme boundary detection), immutable converter, NBest generator,
  and all Japanese dictionary lookup code. These are deeply coupled to
  Japanese morphology and have no future APL use.
- **Keep the converter shell**: retain `ConverterInterface`,
  `EngineConverterInterface`, and `EngineConverter` as thin routing
  layers. The session calls the converter ~30 times and the predictor
  is accessed *through* the converter (`EngineConverter` owns the
  predictor). Deleting these interfaces would require rewriting the
  session's state machine. Instead, strip their internals so that
  `Convert()` is a no-op and `Suggest()`/`Predict()` route to
  predictors. Mozc already has a `MinimalConverter`
  (`src/engine/minimal_converter.cc`) that demonstrates this pattern.
- In Phase 4, APL-specific predictors slot into this shell where the
  Japanese predictor used to live — so the shell is not dead code but
  load-bearing infrastructure for keyword search and idiom completion.

### 0.4 Remove redundant dependencies and submodules
- Delete all GYP-era git submodules (already unused in Bazel builds)
- Remove Japanese Usage Dictionary and Japan Post zip code data from
  `MODULE.bazel`
- Drop Android NDK fetch from `update_deps.py` (desktop-only product)
- Rewrite macOS ObjC tests to remove dependency on Google Toolbox for
  Mac's UnitTesting component (keep Google Toolbox for Mac as a test
  framework dependency)
- Replace Material Design Icons with APL-relevant icons

### 0.5 Rebrand and re-register
- Change product name from "Mozc" to the APL IME product name throughout
  source, installers, and OS registrations
- Register as English (not Japanese) input method on all platforms
  (Windows PoC already does this; apply same to Linux and macOS)
- Update `Info.plist` (macOS), IBus component XML (Linux), and TSF
  profile registration (Windows)

---

## Phase 1 — Shifting Key Input (All Platforms)

**Goal**: Production-quality modifier-held APL glyph input on all three
platforms with configurable shifting keys and multi-locale support.

### 1.1 Fix known PoC issues
- **Linux**: Move Alt glyph output from session to `mozc_engine.cc` to
  use scancodes uniformly; fix L/R Ctrl independence; remove debug logging
- **macOS**: Wire Caps Lock menu item; suppress autocomplete candidate
  window in APL mode; test mode persistence across focus changes
- **Windows**: Implement config persistence (W4) through `InputBehavior`
  and client IPC; implement glyph production (W5) via key intercept in
  `TipKeyeventHandler` with `ITfInsertAtSelection`

### 1.2 Unify glyph table and shifting key architecture
- Consolidate `apl_keymap.h/cc` as the single cross-platform glyph
  lookup (already shared between Linux and macOS branches)
- Standardise the per-platform physical-key-to-character mapping:
  Linux scancodes, macOS virtual keycodes, Windows VK codes all
  resolve to the same ASCII intermediary before glyph lookup
- Ensure shifted (two-modifier) glyphs work consistently

### 1.3 Multi-locale keyboard layout support
- Define locale-specific physical-key-to-ASCII mapping tables for US,
  UK, Nordic (Danish/Finnish/Swedish), and German QWERTY layouts
- The APL glyph table itself stays constant (Dyalog standard); only the
  physical-key-to-intermediary mapping changes per locale
- Add locale selection to config dialog and system tray menu
- Store selected locale in `config.proto`

### 1.4 Platform-specific edge cases
- **Linux/XWayland**: Implement AltGr via XKB symbols file for
  Electron apps where IBus interception fails
- **macOS**: Validate Ctrl+key suppression in Electron/VSCode (IMK
  synchronous model should work, but needs confirmation)
- **Windows**: Validate Caps Lock LED suppression; test L/R modifier
  reliability in UWP and Electron apps
- **All**: Bare-modifier suppression (prevent Alt from activating menu
  bars, Caps Lock from toggling LED)

---

## Phase 2 — Prefix/Dead-Key and Toggle Modes

**Goal**: Implement the two remaining glyph-entry modes using adapted
Mozc composer infrastructure.

### 2.1 Prefix / dead-key mode
- Create APL preedit tables in the existing Mozc TSV format:
  backtick+letter → APL glyph (e.g., `` ` `` then `a` → `⍺`)
- Load tables through the existing `Table` class and Trie lookup
- Wire through composer so the preedit buffer shows the pending backtick
  until the second key resolves or cancels
- Add mode selection (shifting vs. prefix) to config and menus

### 2.2 Toggle mode
- Extend the macOS `CompositionMode::APL` pattern to Linux and Windows
- When toggled on: all letter/symbol keys produce APL glyphs without
  holding a modifier; preedit passthrough bypasses composer
- Toggle trigger: configurable key (e.g., Caps Lock tap, dedicated
  hotkey)
- Visual indicator in system tray / language bar

---

## Phase 3 — Composition Modes (Overstrike & Tab Completion)

**Goal**: Enable building compound APL glyphs from simpler ones.

### 3.1 Overstrike composition
- Define overstrike rules as preedit table entries (e.g., `○|` → `⌽`)
- When two compatible glyphs are adjacent in the preedit buffer,
  automatically compose them
- Limited to preedit buffer (IMEs cannot read application text)
- Use the composer's pending-state mechanism to hold the first glyph
  until the second arrives or a non-composable key commits it

### 3.2 Tab completion
- Define tab-triggered composition rules (e.g., `=>` Tab → `→`)
- Resolve Tab key conflict with existing candidate-selection keymap by
  remapping candidate selection or using a different trigger in APL mode
- Implement as a composer table with the trigger key mapped to commit

---

## Phase 4 — Search and Autocomplete

**Goal**: Leverage the predictor framework for APL glyph discovery
and expression completion.

### 4.1 Keyword search predictor
- Implement `AplKeywordPredictor` behind `PredictorInterface`
- Build a keyword-to-glyph dictionary (e.g., "reverse" → `⌽`,
  "iota" → `⍳`, "rho" → `⍴`) — data sourced from Dyalog documentation
- On partial input, return matching glyphs as candidates in the
  candidate window (renderer)
- Support multiple keyword aliases per glyph

### 4.2 Idiom autocomplete predictor
- Implement `AplIdiomPredictor` behind `PredictorInterface`
- Build an idiom dictionary of common APL expression patterns
  (potentially sourced from APLCart)
- Trigger on APL glyph sequences to suggest completions
- Rank by frequency / user history

### 4.3 Adapt user history predictor
- Verify that the existing `UserHistoryPredictor` works with APL
  glyphs without modification (it should — it tracks selected
  candidates regardless of script)
- If needed, adjust storage encoding for multi-byte UTF-8 glyphs

---

## Phase 5 — Polish, Testing & Release

**Goal**: Production readiness across all platforms and locales.

### 5.1 Config dialog overhaul
- Replace Japanese-specific Qt config panels with APL settings:
  input mode selection, shifting key configuration, locale selection,
  keyword search toggle
- Platform-appropriate entry points (IBus panel, macOS input menu,
  Windows language bar)

### 5.2 Comprehensive cross-platform testing
- Test matrix: 3 platforms × target locales × all input modes ×
  key applications (native text editors, Terminal, VSCode/Electron,
  RIDE)
- Validate modifier suppression, focus persistence, and candidate
  window behaviour
- Automated tests for glyph table correctness and predictor results

### 5.3 Installer and distribution
- **Linux — package manager distribution**:
  - The existing `oss_linux` build already links dynamically against the
    system `libibus-1.0` via `pkg-config` — the `.so` is not bundled in
    the build output (`mozc.zip`). No separate build target is needed.
  - Create `.deb` packaging (Bazel `rules_pkg` provides `pkg_deb`) that
    declares runtime dependencies: `libibus-1.0-0`, `libglib2.0-0`,
    `libqt6core6` / `libqt6gui6` / `libqt6widgets6` (or their
    equivalents in the target distro). The package manager installs these
    from the distro's own repos, so Dyalog is not distributing LGPL
    libraries — only depending on them.
  - Evaluate `.rpm` packaging for Fedora/RHEL if those platforms are in
    scope. The dependency names differ (`ibus-libs`, `qt6-qtbase`, etc.).
  - Survey which target distros ship `libibus-1.0` and `qt6-base` in
    their default or standard repos (Ubuntu 22.04+, Debian 12+, Fedora
    38+ all do; older LTS releases may need a PPA or manual install).
  - For standalone tarball / AppImage distribution (if needed alongside
    `.deb`/`.rpm`), Qt and libibus `.so` files would need to be bundled
    — in that case LGPL obligations apply (see `docs/project-licensing.md`).
  - Update `install.sh`, packaging rules, and installed paths for APL
    branding.
- **macOS — installer and Homebrew Cask**:
  - The existing build produces a signed `.pkg` inside a `.dmg`. Qt
    frameworks are embedded in the app bundle. No separate build target
    is needed for distribution.
  - Update `.pkg` installer, code signing, and `Info.plist` for APL
    branding.
  - **Homebrew**: IME apps require installation into `~/Library/Input
    Methods/`, so distribution must be as a Homebrew **Cask** (prebuilt
    binary), not a formula (source build). A Cask is a Ruby file in
    the `homebrew-cask` repo pointing to the `.dmg` download URL. Casks
    have no shared-library dependency mechanism — Qt frameworks must
    remain bundled in the app bundle (already the case). LGPL
    compliance is via dynamic linking within the bundle (see
    `docs/project-licensing.md`).
  - To publish: submit a Cask definition to `homebrew/homebrew-cask` or
    host a custom tap (`dyalog/homebrew-apl`). The Cask needs a
    stable download URL and SHA-256 hash, both obtainable from GitHub
    Releases.
- **Windows — MSI installer, Chocolatey, and winget**:
  - The existing build produces a WiX `.msi` containing all binaries,
    Qt DLLs, MSVC runtime, and the Qt platform plugin. No separate
    build target is needed for distribution.
  - Update WiX `.wxs` files, TSF registration, and branding for APL.
  - Unlike Linux, Windows package managers have no shared-library
    dependency resolution — everything must be bundled in the MSI. Qt
    DLLs are already shipped as separate swappable files, satisfying
    LGPL via dynamic linking.
  - **Chocolatey**: a package is a `.nuspec` manifest + PowerShell
    install script that downloads and runs the MSI. Publish to
    chocolatey.org or host an internal feed. Requires a stable MSI
    download URL (e.g. GitHub Releases).
  - **winget**: a YAML manifest submitted to Microsoft's
    `winget-pkgs` community repo, pointing to the MSI download URL.
    Manifests are versioned and must pass automated validation.
    Requires a stable download URL with consistent hash.
  - Both Chocolatey and winget are thin wrappers — the MSI is the
    actual distributable. Maintenance cost is low: update the manifest
    version and URL/hash on each release.
- Verify clean install/uninstall on fresh systems across all platforms

### 5.4 Documentation
- User-facing documentation: installation, configuration, input mode
  reference, locale setup
- Developer documentation: architecture overview, how to add glyphs
  or locales, build instructions

---

## Dependency Summary

### Remove
| Dependency | Phase |
|---|---|
| Japanese Usage Dictionary | 0.3 |
| Japan Post zip code data | 0.3 |
| IPAdic / NAIST dictionary | 0.3 |
| Tamachi Phonetic Kanji | 0.3 |
| Android NDK (default fetch) | 0.4 |
| All GYP git submodules | 0.4 |

### Replace
| Dependency | Replacement | Phase |
|---|---|---|
| Material Design Icons | APL-specific icons | 0.4 |
| Japanese preedit tables | APL prefix/overstrike tables | 2.1, 3.1 |
| Japanese keymap tables | APL keymap tables | 0.3 |

### Keep
abseil-cpp, protobuf, googletest, Google Toolbox for Mac, Bazel + all
rulesets, Qt6, IBus, platform toolchains (MSVC/LLVM/Xcode/GCC), WiX,
WIL, rules_apple ecosystem.

---

## Risk Register

| Risk | Impact | Mitigation |
|------|--------|------------|
| XWayland/Electron Ctrl interception failure on Linux | High — RIDE and VSCode are primary APL editors | AltGr via XKB as fallback; push Electron apps to update to Wayland-native |
| Caps Lock LED cannot be suppressed | Medium — cosmetic confusion | Document behaviour; explore per-platform LED control APIs |
| Converter stub breaks session layer | Medium — unexpected codepaths may assume converter output | Thorough testing; implement minimal no-op converter that returns empty results cleanly |
| Multi-locale table maintenance burden | Low–Medium — each locale is a separate mapping file | Generate tables from Dyalog's authoritative layout definitions; automate validation |
| macOS IME cache requires restart after updates | Low — user friction during development | Document workaround; investigate `kTISNotifyEnabledKeyboardInputSourcesChanged` |
