APL IME — Cross-Platform Input Mode Architecture
=================================================

This document describes the full vision for APL glyph input via the Mozc IME
across macOS, Windows, and Linux. It covers the seven input modes, their
composability, technical limitations in the current codebase, and multi-locale
keyboard layout support.

For platform-specific implementation details see:
- [apl_macos.md](apl_macos.md) — macOS / InputMethodKit
- [apl_windows.md](apl_windows.md) — Windows / TSF

---

Input Modes
-----------

The IME supports seven modes of APL glyph entry. Each mode is a distinct
mechanism for producing APL characters or expressions.

### 1. Shifting key input (while-held modifier)

One or more modifier keys, held down, shift the keyboard to an APL layer.
Pressing a character key while the modifier is held inserts the corresponding
APL glyph. For example, APL+a gives ⍺.

**Status**: Implemented on macOS (M1–M5). Windows POC in progress. Linux
branch has a working prototype.

**Implementation**: Platform-level intercept before the Mozc server sees the
key event. Physical key code → ASCII char → `GetAplGlyph(char)` →
`insertText:` / `ITfInsertAtSelection` / `ibus_engine_commit_text`. Bypasses
the composer and prediction pipeline entirely.

### 2. Prefix key input (dead key)

One or more keys act as dead keys. Pressed once, the next key press enters an
APL glyph. For example, backtick then a gives ⍺. The prefix key itself
produces no visible output.

**Status**: Not yet implemented on any platform.

**Implementation**: Requires state at the session or platform level to track
the pending dead key. On the next printable key press, the pair is resolved
to a glyph. Three possible implementation strategies:

- **Composer table extension** — Define entries in the romanji table where
  the prefix character has a pending result (e.g., `{input: "` a", result:
  "⍺", pending: ""}`). The existing `Table` Trie and `Entry`
  pending/result mechanism already handles multi-keystroke sequences. This
  is the cleanest fit for prefix mode but requires loading an APL-specific
  table when APL mode is active.

- **Session-level intercept** — In `Session::InsertCharacter()`, before
  calling the composer, check for a `pending_dead_key_` state variable.
  On the first keystroke, store the dead key and suppress output. On the
  second keystroke, combine and emit the glyph. Minimal changes to the
  composer; state lives at the session level.

- **Platform-level intercept** — Same as the shifting key intercept but
  with a boolean `prefix_pending_` flag in the controller. On prefix key
  press, set the flag and suppress the key. On the next printable key,
  resolve and emit. Simplest to implement but duplicates logic across
  platforms.

**Recommended approach**: Composer table extension (first option). It reuses
existing infrastructure, works identically across platforms, and naturally
integrates with preedit display (the pending state can show a visual
indicator in the composition string).

**Linux caveat**: IBus key event processing is asynchronous. The prefix key
press must be suppressed before the application sees it. On X11 with native
GTK/Qt apps this works. On Wayland and Electron, the async response may
arrive after the app has already processed the key. This is the same issue
documented in `ctrl_key_wip.md` for Ctrl+key. macOS and Windows are
synchronous and unaffected.

### 3. Toggling key input (modal layer switch)

One or more modifier keys toggle APL input mode. Once toggled on, all
character keys produce APL glyphs without needing to hold a modifier. Toggle
off to return to normal input.

**Status**: Partially implemented. `CompositionMode::APL` exists in
`commands.proto`. macOS has Info.plist registration and mode switching
(M2). The behavioural intercept (M5) already handles the "APL mode active,
no shifting modifier held" path by passing through `[event characters]`
directly. A full toggling mode would replace this passthrough with APL
glyph lookup.

**Implementation**: When toggled on, every printable key press goes through
the glyph lookup table. The shifting modifier (if configured) then provides
access to a second layer of shifted APL glyphs. This naturally composes
with shifting key mode — toggle gives base glyphs, shifting modifier gives
shifted glyphs.

### 4. Keyword input (search by name)

The user activates a search mode and types a keyword. The IME searches a
dictionary of APL glyph names and presents matching symbols as candidates.
For example, typing "reverse" or "rotate" shows ⌽ in the candidate list.

**Status**: Not yet implemented on any platform.

**Implementation**: Requires a custom APL predictor that maps English
keywords to APL glyphs. The predictor interface (`PredictorInterface`) is
pluggable — an APL-specific predictor can be registered alongside the
existing Japanese predictor.

**Technical constraint**: The existing prediction query path normalises
input to hiragana via `Composer::GetQueryForPrediction()`. For keyword
search to work, the APL transliterator must pass through raw ASCII input
as the query form, or the query generation must be bypassed for APL mode.
The `AplTransliterator` (currently a passthrough stub on macOS) would need
to preserve the raw input string.

The candidate display infrastructure (preedit + candidate list) works
across all platforms: macOS uses IMK candidate windows, Windows uses TSF
`ITfCandidateString`, Linux uses IBus panel candidates. No new UI is needed.

### 5. Overstrike input (compose by overtyping)

In Insert mode, typing over an existing glyph with a compatible glyph
composes a new glyph. For example, ○ and | give ⌽.

**Status**: Not yet implemented on any platform.

**Implementation**: Requires knowing what glyph is at the cursor position in
the application. IMEs generally cannot read application text — they only see
their own preedit buffer.

**Technical constraint — surrounding text access**:
- macOS: `selectedRange` / `markedRange` exist on `IMKTextInput` but
  reading surrounding text is unreliable across apps
- Windows: `ITfRange::GetText` can read surrounding text in some TSF-aware
  apps but not universally
- Linux: `ibus_engine_get_surrounding_text()` works in GTK apps but not in
  Electron or many Qt apps

**Realistic scope**: Overstrike can work **within the preedit** (before
committing) but not reliably on previously committed text. This limits
it to composing glyphs during active composition — still useful, but
not the full "type over existing text" experience. A full overstrike
mode would require editor cooperation (e.g., a plugin protocol).

### 6. Tab completion (compose by trigger key)

Similar to overstrike but with a broader set of compatible glyph pairs and
a postfix trigger key. Typing glyphs and then pressing Tab (or another
trigger) causes them to compose. For example, `=>` then Tab gives →.

**Status**: Not yet implemented on any platform.

**Implementation**: Maintains a compose buffer of recently typed glyphs.
On the trigger key press, attempts to match the buffer tail against a
composition table. If a match is found, replaces the matched characters
with the composed glyph.

**Technical constraint — Tab key conflict**: Tab is already mapped in the
session keymap for candidate selection (`SegmentFocusRight`,
`PredictAndConvert`). Options:
- Use a different trigger key (e.g., `;` or a configurable key)
- Add APL-mode-specific keymap entries that override the default Tab binding
  (the keymap system in `keymap.h` supports per-state bindings)
- Handle it at the platform intercept level before the session sees it

Tab completion operates on the preedit buffer, so it faces similar
constraints to overstrike regarding committed text. However, within an
active composition, the preedit content is fully accessible and
replaceable.

### 7. Idiom search / autocomplete

In this mode, typing APL glyphs searches through a collection of idioms
and suggests completions for longer expressions. For example, typing `+/`
might suggest `+/⍳` (sum of integers) as a completion.

**Status**: Not yet implemented on any platform.

**Implementation**: Requires a custom idiom dictionary and predictor. The
infrastructure is the same as keyword search (mode 4) — a pluggable
predictor that returns candidates based on the current composition. The
difference is that the query is APL glyphs rather than English keywords.

**Technical constraint**: Same as keyword search — the prediction query
path normalises to hiragana. The APL transliterator must preserve glyph
sequences as the query form.

---

Composability
-------------

### Layer model

The seven modes fall into three orthogonal layers:

| Layer | Modes | Purpose |
|-------|-------|---------|
| **Glyph entry** | 1 (shifting), 2 (prefix), 3 (toggle) | How a single APL glyph is typed |
| **Glyph composition** | 5 (overstrike), 6 (tab completion) | Combining entered glyphs into new ones |
| **Search / suggestion** | 4 (keyword), 7 (idiom autocomplete) | Finding glyphs or expressions by name or pattern |

Cross-layer composition works naturally. A user can enter glyphs via
shifting key (layer 1), compose them with tab completion (layer 2), and
get idiom suggestions (layer 3). These do not conflict because they have
different triggers and operate on different units of input.

### Within-layer compatibility

**Glyph entry (1, 2, 3):**

- **1 + 3 (shifting + toggle)**: Compatible and complementary. Toggled mode
  produces base APL glyphs; the shifting modifier produces shifted APL
  glyphs (e.g., `a` → `⍺` in toggled mode, `APL+a` → `⍶` via shifting).
  This is already how the macOS M5 intercept works.

- **1 + 2 (shifting + prefix)**: Fully compatible. Different triggers — held
  modifier vs. dead key press. No conflict.

- **2 + 3 (prefix + toggle)**: Requires care. In toggled APL mode, the dead
  key itself maps to an APL glyph. The prefix key must be reserved from the
  APL glyph table, or it must be a key outside the printable range (e.g., a
  function key or a dedicated key like the backtick on US QWERTY, which
  would then not produce its APL glyph directly).

**Glyph composition (5, 6):**

- **5 + 6 (overstrike + tab completion)**: These are variations of the same
  concept. They can coexist if overstrike is tied to Insert mode and tab
  completion to normal mode, but having both active simultaneously is
  confusing. Treat these as user-selectable alternatives.

**Search / suggestion (4, 7):**

- **4 + 7 (keyword + idiom)**: These operate on different input types
  (English keywords vs. APL glyph sequences) and could share the candidate
  list UI. However, only one search mode should be active at a time to
  avoid confusing candidate mixing.

### Orchestration

A state machine at the session level with orthogonal flags:

```
APL Input State
  glyph_entry_mode:   shifting | prefix | toggle | off   (one active)
  composition_mode:   overstrike | tab | off              (one active)
  search_mode:        keyword | idiom | off               (one active)

  prefix_pending:     optional<key>       (dead key state for mode 2)
  compose_buffer:     vector<glyph>       (for modes 5/6)
```

Each incoming key event is evaluated against these flags in priority order:
prefix pending → glyph entry → composition → search. This keeps the modes
composable without combinatorial explosion.

Note that shifting + toggle compose naturally without explicit orchestration:
toggle sets the base layer, shifting accesses the shifted layer. This is a
property of the glyph lookup, not the state machine.

### Recommended default combination

**(1 + 3) + 2 + 6 + 7**: Shifting and toggle for glyph entry, prefix for
extended glyphs, tab completion for composition, idiom autocomplete for
suggestions. This covers the common APL input workflows without mutual
exclusion conflicts.

---

Technical Limitations
---------------------

### 1. The shifting key intercept bypasses the Mozc pipeline

The macOS M5 and planned Windows W5 implementations call `insertText:` /
`ITfInsertAtSelection` directly, skipping the Mozc server, composer, and
prediction pipeline. This is correct for modes 1 (shifting) and 3 (toggle)
where the goal is immediate glyph emission. But modes 4, 5, 6, and 7
require **preedit display** and **candidate lists**, which depend on the
composer and prediction pipeline.

**Implication**: Modes that need preedit/candidates must route input through
the Mozc server rather than short-circuiting at the platform layer. This
means the mode 1/3 intercept (platform-level, before server) and the mode
4/5/6/7 path (through server, using composer + predictor) are architecturally
distinct code paths. The orchestration state machine selects which path to
take.

### 2. The composer table is hiragana-centric

The `Table` in `src/composer/table.h` is a Trie mapping romanji sequences to
hiragana. The transliterator is display-only — it transforms hiragana output
for rendering but does not change the composition logic.

For prefix key mode (mode 2), the table approach works well: entries like
`{input: "` a", result: "⍺"}` are structurally identical to `{input: "ka",
result: "か"}`. An APL-specific table can be loaded when APL mode is active.
The `Table::LoadFromFile()` mechanism supports runtime table swapping.

For tab completion (mode 6), a similar approach works if the compose
sequences are defined as table entries.

### 3. The prediction system cannot find APL glyphs

`Suggest()` in `session.cc` calls the predictor with a hiragana query
derived from the composer via `GetQueryForPrediction()`. For keyword search
(mode 4) and idiom autocomplete (mode 7), a custom predictor is needed that
maps:
- English keywords → APL glyphs (mode 4)
- APL glyph sequences → idiom completions (mode 7)

The predictor interface (`PredictorInterface::PredictForRequest`) is
pluggable, so an APL-specific predictor can be registered. The query
generation path must be extended to pass through raw input (not
hiragana-normalised) when in APL mode.

### 4. Overstrike cannot read application text

IMEs generally cannot read text that has already been committed to the
application. The `SurroundingTextInfo` protocol exists but is unreliably
supported across apps and platforms. Overstrike (mode 5) is limited to
operating within the preedit buffer.

### 5. Linux async key handling affects prefix/dead key mode

IBus `ProcessKeyEvent()` returns asynchronously via D-Bus. For prefix key
mode (mode 2), the dead key press must be suppressed before the application
sees it. If the async response arrives late, the application may have
already processed the key. This affects Wayland and Electron apps. X11 with
native GTK/Qt is unaffected. macOS and Windows are synchronous.

### 6. Tab conflicts with existing keymap bindings

Tab is mapped in the session keymap for candidate selection. APL-mode-
specific keymap entries would override the default Tab binding, or a
different trigger key can be used.

---

Multi-Locale Support
--------------------

Supporting APL input across different keyboard layouts and OS locales
involves two separate concerns: OS registration (telling the system the
IME is available for a language) and key mapping (ensuring physical keys
map correctly regardless of layout).

### OS registration per locale

**macOS** — Each locale is a static entry in `Info.plist` under
`ComponentInputModeDict`. Adding a locale means adding a dict with a
`TISInputSourceID` and `TISIntendedLanguage`. This is build-time
configuration — all supported locales must be declared in the plist.
There is no dynamic registration API.

**Windows** — Profile registration uses `ITfInputProcessorProfileMgr::
RegisterProfile()` with a `LANGID`. Multiple profiles can be registered
under the same CLSID with different profile GUIDs and language IDs. The
current POC registers a single English profile; adding French, German,
etc. means additional `RegisterProfile()` calls in the installer. This
is the standard pattern for multi-language IMEs.

**Linux** — IBus reads engine definitions from XML config files. No code
change is needed per locale, only config.

**Verdict**: Registration is mechanical work, not a technical challenge.
The main annoyance is macOS requiring static plist entries, but that is
build configuration rather than code.

### Physical key codes across layouts

The APL glyph mapping uses a two-stage pipeline:

```
Physical key → ASCII char → APL glyph
  (platform)    (unified)
```

The second stage (`GetAplGlyph('a')` → `"⍺"`) is already
locale-independent — it takes an ASCII character and returns a glyph.
The question is whether the first stage (physical key → ASCII char) works
across layouts.

**Letters (A–Z)**: No problem on any platform.
- macOS Carbon virtual key codes (`kVK_ANSI_A = 0x00`) are physical
  position, layout-independent
- Windows VK codes for letters (`VK_A = 0x41`) are always A–Z regardless
  of keyboard layout
- Linux evdev scancodes are physical position

**Symbols / punctuation (`[`, `]`, `;`, `/`, etc.)**:
- **macOS**: Carbon vkeys like `kVK_ANSI_LeftBracket` are physical.
  `apl_keycode_map.h` maps these to US QWERTY ASCII. Works regardless of
  layout.
- **Linux**: evdev scancodes are physical. Same approach works.
- **Windows**: `VK_OEM_*` codes are **layout-dependent** for symbol keys.
  `VK_OEM_4` is `[` on US QWERTY but `ü` on German and `^` on French.
  Fix: use scan codes from `LPARAM` bits 16–23 (these are physical) or
  call `MapVirtualKeyEx(vk, MAPVK_VK_TO_VSC)` to get the scan code,
  then map scan code → US QWERTY char.

### Same glyph layout, many locales

If the goal is the standard Dyalog APL keyboard (defined in terms of US
QWERTY positions) available under multiple OS languages, the current
physical-key-based architecture already works. The same unified glyph
table is used regardless of locale. The user's keycap labels will not
match the APL assignments on non-US keyboards, but the physical positions
are correct. This is the standard trade-off for APL keyboards and is
typically addressed with keyboard overlays or reference cards.

Register the IME under each desired LANGID (Windows) or
`TISIntendedLanguage` (macOS) and use the same glyph lookup code. No
per-locale glyph table is needed.

### Per-locale glyph layouts

If different locales should have different APL glyph assignments (e.g., a
French APL layout optimised for AZERTY ergonomics), the glyph table needs
to be parameterised by locale:

```cpp
class AplKeymap {
 public:
  static const AplKeymap& Get(absl::string_view locale);  // "en-US", "fr-FR"
  std::optional<absl::string_view> GetGlyph(uint32_t key_code) const;
  std::optional<absl::string_view> GetShiftedGlyph(uint32_t key_code) const;
};
```

The session or platform layer selects the keymap based on the active
profile's locale. This is a data-driven table swap — the lookup mechanism
is unchanged.

For prefix key mode, the same structure applies. A prefix table entry maps
`(prefix_key, following_key)` → glyph. If the prefix key is always the
same physical key (e.g., backtick), it works across layouts via physical
key codes. If locale-specific prefix keys are needed (e.g., `²` on French
AZERTY instead of backtick), that is a config entry per locale.

### Summary

| Concern | Difficulty | Notes |
|---------|-----------|-------|
| macOS locale registration | Easy | Plist entries, no code changes |
| Windows locale registration | Easy | Multiple `RegisterProfile()` calls |
| Linux locale registration | Trivial | Config only |
| Letters (A–Z) across layouts | Already works | Physical codes on all platforms |
| Symbols across layouts | Easy fix on Windows | Use scan codes instead of `VK_OEM_*` |
| Same glyph layout, many locales | Already works | Single table, physical keys |
| Per-locale glyph layouts | Moderate | Data-driven table swap, locale from active profile |
| Per-locale prefix keys | Easy | Config entry per locale |

---

Key Event Flow (Cross-Platform)
--------------------------------

### Platform entry points

| Platform | Entry File | Function | Sync? |
|----------|-----------|----------|-------|
| macOS | `src/mac/mozc_imk_input_controller.mm` | `handleEvent:client:` | Synchronous |
| Windows | `src/win32/tip/tip_keyevent_handler.cc` | `OnTestKeyDown` / `OnKeyDown` | Synchronous |
| Linux | `src/unix/ibus/mozc_engine.cc` | `ProcessKeyEvent` | Asynchronous |

### Interception strategies

**Pattern A — Platform-level intercept (before server)**: Used for modes
1 and 3 (shifting key, toggle). The platform layer checks APL mode and
modifier state, resolves the glyph from the physical key code, and inserts
text directly. The Mozc server is never involved.

Pros: lowest latency, no server round-trip, clean keystroke suppression.
Cons: no preedit, no candidates, no composition state.

**Pattern B — Server-level intercept (session layer)**: Used for modes 2,
4, 6, and 7 (prefix, keyword, tab completion, idiom). Input is translated
to a `commands::KeyEvent` and sent to the session. The session maintains
APL-specific state (dead key pending, compose buffer) and routes to the
APL predictor for candidates.

Pros: full access to preedit, candidates, and composition state.
Cons: requires extending the session and composer for APL awareness.

**Pattern C — Hybrid**: The platform intercept handles shifting/toggle
(pattern A). All other input passes through the normal Mozc path to the
session, which handles prefix keys, composition, and search (pattern B).
The orchestration state machine at the platform level selects the path.

Pattern C is the recommended architecture. It keeps the fast path fast
(shifting key has no server overhead) while enabling rich interaction for
advanced modes.

---

Key Files
---------

### Shared (cross-platform)

| File | Role |
|------|------|
| `src/session/apl_keymap.h` / `.cc` | APL glyph lookup: `GetAplGlyph(char)`, `GetAplShiftedGlyph(char)` |
| `src/protocol/commands.proto` | `CompositionMode::APL`, `KeyEvent`, `Output` |
| `src/protocol/config.proto` | `AplShiftingKeySet` message (field 122 on `Config`) |
| `src/session/session.cc` | Core state machine: `ApplyCompositionMode()`, `InsertCharacter()` |
| `src/composer/composer.cc` | Composition pipeline: `InsertCharacterKeyEvent()`, transliterator dispatch |
| `src/composer/table.h` / `.cc` | Romanji table (Trie of Entry objects) — extensible for APL prefix entries |
| `src/composer/transliterators.h` / `.cc` | Display transformation system — `AplTransliterator` for APL mode |

### macOS

| File | Role |
|------|------|
| `src/mac/mozc_imk_input_controller.mm` | IMK frontend: `handleEvent:` intercept, shifting key cache, NSMenu handler |
| `src/mac/apl_keycode_map.h` | Carbon virtual key code → US QWERTY ASCII char |
| `src/mac/KeyCodeMap.mm` | Key event translation; strips Caps Lock, blocks Cmd |
| `src/mac/Info.plist` | Input mode registration (`ComponentInputModeDict`) |
| `src/mac/English.lproj/Config.xib` | NSMenu definition for shifting key submenu |

### Windows

| File | Role |
|------|------|
| `src/win32/tip/tip_keyevent_handler.cc` | TSF key event processing: `OnKeyDown` intercept point |
| `src/win32/tip/tip_lang_bar.cc` | Language bar: APL shifting key menu with checkmarks |
| `src/win32/tip/tip_lang_bar_callback.h` | Menu item IDs (`kAplShiftingKeyLeftCtrl` etc.) |
| `src/win32/base/tsf_profile.cc` | Profile registration (LANGID, CLSID) |
| `src/win32/base/input_state.h` | `InputBehavior` struct — live config for shifting key state |
| `src/win32/base/config_snapshot.cc` | Startup config cache — initial load of shifting key prefs |

### Linux

| File | Role |
|------|------|
| `src/unix/ibus/mozc_engine.cc` | IBus frontend: `ProcessKeyEvent` intercept point |
| `src/unix/ibus/key_event_handler.cc` | Key translation: evdev scancode → `commands::KeyEvent` |
