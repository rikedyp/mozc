APL Shifting-Key Input Mode
===========================

Objective
---------

Enable APL glyph input by designating one or more modifier keys as an "APL shifting key". While the shifting key is held, ordinary key presses produce APL glyphs instead of their Latin equivalents — mirroring the physical APL keyboard overlays used historically and in modern APL environments.

Background
----------

Traditional APL keyboards assign APL glyphs to the shifted or "APL-shifted" positions of standard keys. On a modern keyboard without a physical APL overlay, this is typically emulated by using a modifier key as a mode-shift: holding the modifier causes each key to emit the APL glyph assigned to that key position.

Different APL implementations use different shifting keys. We want to experiment with several candidates to find what works best on Linux/IBus and is least likely to conflict with existing system or application shortcuts.

Candidate Shifting Keys
-----------------------

| Modifier | Notes |
|----------|-------|
| Ctrl | Standard APL keyboard convention on many platforms; highest priority for initial experiments. May conflict with application shortcuts (Ctrl+C, Ctrl+V, etc.). |
| Alt / Meta | Common in terminal-based APL environments. Risk of conflict with application menu shortcuts. |
| AltGr (Right Alt) | Used by GNU APL and Dyalog APL on Linux. Less conflict with common shortcuts; promising candidate. |
| Super (Windows key) | Largely reserved by the desktop environment (GNOME, KDE) for system shortcuts; may be difficult to intercept reliably. |
| Caps Lock | Repurposing Caps Lock as a latching APL-shift key is an option; avoids hold-down ergonomic issues. Requires key remapping at a lower level. |

Goals
-----

1. Implement Ctrl as the initial APL shifting key, producing APL glyphs for the standard APL key assignments while Ctrl is held.
2. Evaluate feasibility of AltGr as an alternative or additional shifting key, as it is the most conflict-free candidate on Linux.
3. Assess whether Alt, Super, and Caps Lock can be supported within the IBus/Mozc architecture.
4. Ensure the shifting-key mode can be toggled or configured per user, rather than being hardcoded.

Open Questions
--------------

- Which keys in the IBus key event pipeline are accessible as modifiers vs. intercepted by the compositor or kernel?
- Can Caps Lock be intercepted at the IBus level, or does it require an XKB or udev rule?
- Should the shifting key be configurable at runtime (e.g. via an IBus preference panel), or is a compile-time default sufficient for now?

---

Architecture Analysis
=====================

How Key Events Flow Through Mozc
---------------------------------

Understanding the existing pipeline is essential before deciding where to inject APL behaviour.

```
IBus (user presses Ctrl+A)
  │
  ▼
MozcEngine::ProcessKeyEvent()          [src/unix/ibus/mozc_engine.cc]
  │  Converts IBus keyval/modifiers → Mozc KeyEvent via KeyTranslator
  │
  ▼
client->SendKeyWithContext(key, ctx)
  │
  ▼
Session::SendKey(command)               [src/session/session.cc]
  │  Dispatches by ImeContext state: DIRECT / PRECOMPOSITION / COMPOSITION / CONVERSION
  │
  ▼
keymap->GetCommand{State}(key, &cmd)    [src/session/keymap.cc]
  │  Looks up (state, key+modifiers) in keymap tables (e.g. ms-ime.tsv)
  │  Returns a command enum: INSERT_CHARACTER, COMMIT, MOVE_CURSOR, etc.
  │
  ▼
Session handler (switch on command)
  │  e.g. INSERT_CHARACTER → Composer::InsertCharacterKeyEvent(key)
  │
  ▼
Composer → Table::LookUpPrefix()        [src/composer/table.cc]
  │  Matches key sequence against preedit table (e.g. romanji-hiragana.tsv)
  │  Preedit tables work on CHARACTER SEQUENCES, not modifier keys
  │
  ▼
Converter → Rewriter → Output
```

### Key Insight: Two Separate Mapping Systems

Mozc has **two** mapping systems that operate at different levels:

1. **Keymap** (`src/data/keymap/*.tsv`): Maps `(state, key+modifiers) → command`.
   Handles Ctrl+A → MoveCursorToBeginning, Ctrl+M → Commit, etc.
   This is where modifier keys are consumed.

2. **Preedit table** (`src/data/preedit/*.tsv`): Maps `character_sequence → output_string`.
   Handles `ka → か`, `zh → ←`, etc. Operates on plain characters only;
   modifier keys do **not** participate in preedit table lookups.

**Consequence**: APL shifting-key input (Ctrl+A → ⍺) cannot be implemented via
preedit tables alone. It must be handled at or before the keymap level.

### Current Ctrl+Key Bindings (ms-ime.tsv)

```
Composition  Ctrl a  MoveCursorToBeginning
Composition  Ctrl d  MoveCursorRight
Composition  Ctrl h  Backspace
Composition  Ctrl i  ConvertToFullKatakana
Composition  Ctrl m  Commit
Composition  Ctrl u  ConvertToHiragana
...
```

These would conflict with APL assignments. In APL mode, Ctrl+A must produce ⍺
instead of MoveCursorToBeginning.

### Modifier Keys Available via IBus

The IBus layer passes **all** modifier combinations to Mozc without filtering.
The `KeyEvent` proto supports:

```protobuf
enum ModifierKey {
  CTRL = 1;  ALT = 2;  SHIFT = 4;
  LEFT_CTRL = 32;  LEFT_ALT = 64;  LEFT_SHIFT = 128;
  RIGHT_CTRL = 256;  RIGHT_ALT = 512;  RIGHT_SHIFT = 1024;
  CAPS = 2048;
}
```

AltGr arrives as `RIGHT_ALT`. Left/right Ctrl are distinguishable.
Super/Meta is **not** in the enum — it's typically consumed by the desktop environment.

> **Note on AltGr and XKB**: The `RIGHT_ALT` representation above describes what Mozc
> sees *if* the keystroke reaches it as a modifier+base_key pair. On Linux, AltGr
> keystrokes are processed by XKB before IBus. If the active keyboard layout defines
> AltGr compositions for a key (e.g. AltGr+e → '€' on a Euro layout), XKB resolves
> the combination into a single character and IBus delivers that character — not a
> `(RIGHT_ALT, base_key)` pair — to Mozc. The modifier+key_code model assumed here
> therefore applies only when the XKB layout leaves the relevant AltGr+key combinations
> unbound. Any AltGr-based shifting implementation must be tested across common keyboard
> layouts and may require XKB cooperation (e.g. a custom layout that leaves AltGr
> combinations free for Mozc to handle).

### Composition Modes

Mozc defines modes in `protocol/commands.proto`:

```protobuf
enum CompositionMode {
  DIRECT = 0;  HIRAGANA = 1;  FULL_KATAKANA = 2;
  HALF_ASCII = 3;  FULL_ASCII = 4;  HALF_KATAKANA = 5;
}
```

Adding a new `APL` mode here would give us a clean toggle mechanism and
integrate with the existing property/menu system.

---

Implementation Plan
===================

The plan is divided into phases. Phase 1 is the minimum viable proof-of-concept
on Linux. Later phases add polish, cross-platform support, and advanced features.

Phase 1: Minimal Linux POC (Ctrl+key → APL glyph)
--------------------------------------------------

**Goal**: Prove the mechanism works end-to-end on Linux/IBus. When the IME is
in "APL mode" and the user presses Ctrl+A, the character ⍺ is committed to the
application. No preedit, no conversion — direct commit.

### Approach

Intercept key events at the **session level**, before the normal keymap lookup.
When APL mode is active and Ctrl is held, look up the base key in a hardcoded
APL glyph table and commit the result directly. This bypasses the
keymap/composer/converter pipeline entirely, which is correct because APL
shifting-key input has no composition or conversion semantics.

### Step 1.1: Add APL composition mode to the protocol

**File**: `src/protocol/commands.proto`

Add `APL = 6` to `CompositionMode`:

```protobuf
enum CompositionMode {
  DIRECT = 0;
  HIRAGANA = 1;
  FULL_KATAKANA = 2;
  HALF_ASCII = 3;
  FULL_ASCII = 4;
  HALF_KATAKANA = 5;
  APL = 6;                // NEW
  NUM_OF_COMPOSITIONS = 7; // was 6
}
```

This gives APL mode first-class status in the protocol, enabling mode switching
via the existing `SessionCommand::SWITCH_COMPOSITION_MODE` mechanism.

### Step 1.2: Create APL glyph mapping table

**New file**: `src/session/apl_keymap.h`

A simple header-only lookup table mapping ASCII key codes to APL glyphs.
Initial mapping based on the Dyalog APL US keyboard layout:

```cpp
#ifndef MOZC_SESSION_APL_KEYMAP_H_
#define MOZC_SESSION_APL_KEYMAP_H_

#include <cstdint>
#include <optional>
#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

// Returns the APL glyph for a given ASCII key code when Ctrl is held,
// or std::nullopt if no mapping exists.
std::optional<absl::string_view> GetAplGlyph(uint32_t key_code);

}  // namespace session
}  // namespace mozc

#endif
```

**New file**: `src/session/apl_keymap.cc`

```cpp
#include "session/apl_keymap.h"
#include "absl/container/flat_hash_map.h"

namespace mozc {
namespace session {

std::optional<absl::string_view> GetAplGlyph(uint32_t key_code) {
  // Dyalog-style APL keyboard layout (Ctrl+key)
  static const auto* kMap = new absl::flat_hash_map<uint32_t, absl::string_view>({
    {'a', "⍺"},  {'w', "⍵"},  {'e', "∊"},  {'r', "⍴"},
    {'t', "∼"},  {'y', "↑"},  {'u', "↓"},  {'i', "⍳"},
    {'o', "○"},  {'p', "⋆"},  {'d', "⌊"},  {'f', "⌈"},    // ... etc — full layout to be defined
    {'s', "⌈"},  {'l', "⎕"},  {'n', "⊤"},  {'m', "∣"},
    {'z', "⊂"},  {'x', "⊃"},  {'c', "∩"},  {'v', "∪"},
    {'b', "⊥"},  {'j', "∘"},  {'k', "'"},  {'g', "∇"},
    {'h', "∆"},  {'q', "?"},
    {'[', "←"},  {']', "→"},  {'\\', "⍀"}, {'/','⌿'},
    {'=', "÷"},  {'-', "×"},  {'.', "⍀"},  {',', "⍪"},
    {'1', "¨"},  {'2', "¯"},  {'3', "<"},  {'4', "≤"},
    {'5', "="},  {'6', "≥"},  {'7', ">"},  {'8', "≠"},
    {'9', "∨"},  {'0', "∧"},
  });
  auto it = kMap->find(key_code);
  if (it != kMap->end()) return it->second;
  return std::nullopt;
}

}  // namespace session
}  // namespace mozc
```

The exact glyph assignments will be refined. This is a starting point based on
common APL keyboard layouts.

### Step 1.3: Intercept Ctrl+key in Session when in APL mode

**File**: `src/session/session.cc`

Add an early check in `SendKey()` or in each state handler. The cleanest
injection point is a new helper called before the keymap lookup:

```cpp
// New helper method on Session:
bool Session::TryAplShiftedKey(commands::Command* command) {
  // Check if we're in APL mode
  if (context_->GetRequest().composition_mode() != commands::APL) {
    return false;
  }

  const commands::KeyEvent& key = command->input().key();

  // Check if Ctrl is held (the shifting key)
  bool has_ctrl = false;
  for (int i = 0; i < key.modifier_keys_size(); ++i) {
    if (key.modifier_keys(i) == commands::KeyEvent::CTRL) {
      has_ctrl = true;
      break;
    }
  }
  if (!has_ctrl || !key.has_key_code()) return false;

  // Look up APL glyph
  auto glyph = session::GetAplGlyph(key.key_code());
  if (!glyph.has_value()) return false;

  // Direct commit — set the result and mark consumed
  commands::Result* result = command->mutable_output()->mutable_result();
  result->set_type(commands::Result::STRING);
  result->set_value(std::string(*glyph));
  result->set_key(std::string(*glyph));
  command->mutable_output()->set_consumed(true);
  return true;
}
```

Call this at the top of `SendKeyPrecompositionState()`,
`SendKeyCompositionState()`, and `SendKeyConversionState()`:

```cpp
bool Session::SendKeyCompositionState(commands::Command* command) {
  if (TryAplShiftedKey(command)) return true;  // APL intercept
  // ... existing code ...
}
```

This ensures APL shifted keys take priority over normal keymap bindings
whenever APL mode is active.

### Step 1.3a: Guard `Composer::SetInputMode` for APL mode

**File**: `src/session/session.cc`, `src/session/session.h`

When `SWITCH_COMPOSITION_MODE` arrives with `commands::APL`, the session's existing
handler calls `Composer::SetInputMode(mode)`. `Composer::SetInputMode()` has explicit
cases for HIRAGANA, FULL_KATAKANA, HALF_ASCII, etc., but no case for `APL`. Passing
`APL` reaches the default case, which will either log an error or leave the Composer
in an undefined transliteration state.

The fix has two parts:

**Part A — Set the Composer to passthrough for APL mode.**
In the session's composition-mode switching code, intercept APL before the
`SetInputMode` call and substitute `HALF_ASCII`. HALF_ASCII is a literal-passthrough
mode that does no transliteration — which is exactly right, since `TryAplShiftedKey()`
bypasses the Composer entirely for every shifted keystroke anyway.

```cpp
// In Session::SwitchCompositionMode() (or equivalent):
if (mode == commands::APL) {
  apl_mode_active_ = true;
  composer_.SetInputMode(commands::HALF_ASCII);  // passthrough; APL bypasses Composer
  return;
}
apl_mode_active_ = false;
composer_.SetInputMode(mode);  // existing path for all other modes
```

**Part B — Persist APL mode in a dedicated flag.**
`TryAplShiftedKey()` needs to know whether APL mode is currently active. Rather than
reading from the incoming `Request` (which reflects the client's per-call request, not
the session's persisted state), use a dedicated member on `Session` (or `ImeContext`):

```cpp
// src/session/session.h — add to Session private members:
bool apl_mode_active_ = false;
```

Replace the `composition_mode()` check in `TryAplShiftedKey()` with this flag:

```cpp
bool Session::TryAplShiftedKey(commands::Command* command) {
  if (!apl_mode_active_) return false;
  // ... rest unchanged ...
}
```

**Files changed** (additions to the Phase 1 table):

| File | Change |
|------|--------|
| `src/session/session.h` | Add `apl_mode_active_` member |
| `src/session/session.cc` | Guard `SetInputMode` for APL; set/clear `apl_mode_active_` in mode-switch handler; use flag in `TryAplShiftedKey()` |

### Step 1.4: Add APL mode to the IBus property menu

**File**: `src/unix/ibus/property_handler.cc`

Add an "APL" entry to the composition mode menu, alongside Hiragana, Katakana,
etc. This lets the user switch to APL mode from the IBus language bar.

The property handler already has a pattern for this — it creates radio-button
properties for each mode. Add one more:

```cpp
// In PropertyHandler initialization, add:
// "InputMode.APL" property with label "APL" and icon
```

**File**: `src/unix/ibus/property_handler.h`

Add the APL property member alongside the existing mode properties.

### Step 1.5: Handle APL mode in PropertyHandler

**File**: `src/unix/ibus/property_handler.cc`

In `ProcessPropertyActivate()`, handle the new `"InputMode.APL"` property
name and send `SWITCH_COMPOSITION_MODE` with `commands::APL`.

In `UpdateCompositionModeIcon()`, add a case for `commands::APL` to set
the appropriate icon/label.

### Step 1.6: Build and test

```sh
cd src && bazel build unix/ibus:ibus_mozc
```

Test manually:
1. Install the built IBus component
2. Select the Mozc IME
3. Switch to APL mode via the language bar
4. Press Ctrl+A → expect ⍺ to appear in the text field
5. Press Ctrl+W → expect ⍵
6. Release Ctrl, press A → expect normal 'a'

### Files changed in Phase 1

| File | Change |
|------|--------|
| `src/protocol/commands.proto` | Add `APL = 6` to `CompositionMode` |
| `src/session/apl_keymap.h` | New: APL glyph lookup |
| `src/session/apl_keymap.cc` | New: APL glyph table |
| `src/session/session.h` | Declare `TryAplShiftedKey()`; add `apl_mode_active_` member |
| `src/session/session.cc` | Implement `TryAplShiftedKey()`, call from state handlers; guard `SetInputMode` for APL in mode-switch handler |
| `src/session/BUILD.bazel` | Add `apl_keymap` target |
| `src/unix/ibus/property_handler.cc` | Add APL menu item and handler |
| `src/unix/ibus/property_handler.h` | Add APL property member |

### Risks and mitigations

- **Ctrl+C/V/X conflict**: In APL mode, Ctrl+C would produce a glyph instead
  of copying. Mitigation: document this clearly; later phases can add an
  exception list or use a different shifting key.
- **Proto versioning**: Adding a new enum value to `CompositionMode` is
  backward-compatible in protobuf (unknown values are preserved).
- **Composition state interaction**: If the user has preedit text and presses
  Ctrl+A in APL mode, we commit the APL glyph but the preedit remains. May
  need to commit or discard preedit first. Address in Phase 2.

### Note on fcitx compatibility

fcitx-mozc is maintained outside this repository (community patches / separate
repo). However, fcitx-mozc is just a thin frontend client — it converts
platform key events to Mozc `KeyEvent` protobufs and sends them to the **same
Mozc server binary** via IPC. The session-level changes in Phase 1 (proto,
apl_keymap, `TryAplShiftedKey()`) run server-side and therefore work for
fcitx clients automatically with no additional code. The only fcitx-specific
work would be adding the "APL" menu item to fcitx's mode UI, which is a small
task in the fcitx-mozc frontend code (out of scope for this repo). No separate
verification stage is needed.

### Phase 1 Result — Verified ✓

**Platform**: Linux / Wayland / KDE
**Build**: `bazelisk build package --config oss_linux` (debug/fastbuild)

> **Note on testing after rebuild**: `install.sh` restarts `ibus-engine-mozc`
> (the IBus frontend) but not `mozc_server` (the session/converter daemon).
> Since session-level changes (e.g. `OutputMode`, `TryAplShiftedKey`,
> `apl_mode_active_`) run inside `mozc_server`, a stale server process will
> mask any changes. After installing, run `killall mozc_server` — it will
> relaunch automatically on the next keystroke.

Phase 1 is complete. Ctrl+key input successfully produces APL glyphs when the
IME is in APL mode. End-to-end verification confirmed on Linux/Wayland/KDE.

**Issues found during testing:**

1. **Taskbar icon shows "_A" instead of the APL glyph.** The IBus panel label
   for APL mode is rendering incorrectly. Likely a string or lookup issue in
   `UpdateCompositionModeIcon()`.
   
2. **Autocomplete popup appears during normal (unshifted) typing.** Keystrokes
   without Ctrl pass through as HALF_ASCII text and trigger Mozc's
   suggestion/prediction UI. This is distracting in APL mode, where users are
   not composing Japanese text.

3. **APL mode reverts to Latin after autocomplete interaction.** Reproduction:
   - Type a key that opens the autocomplete popup (e.g. `+` or `=`)
   - Press Enter (commits or dismisses the popup)
   - Press `Ctrl =` → produces `÷` correctly
   - Press `Ctrl =` again → **fails**: Mozc has reverted to Latin mode; Ctrl
     combinations now perform application shortcuts (e.g. Ctrl+A → select all,
     Ctrl+= → zoom in)

   Likely cause: the Enter keypress while the popup is visible triggers a
   session state transition that clears `apl_mode_active_`. Once that flag is
   false, `TryAplShiftedKey()` returns early and Ctrl events pass through to
   the application unhandled.

4. **Ctrl+key input does not work in VSCode (Electron/Chromium apps).**
   APL glyphs are produced correctly in native applications (e.g. Kate) but
   not in VSCode. Clicking into a VSCode editor also causes the Mozc mode
   indicator to revert to Latin.

   **Initial hypothesis (INCORRECT)**: Electron (Chromium) intercepts
   Ctrl+key combinations at the application level before they reach IBus.

   **Actual root cause**: Verified via file-based logging in
   `MozcEngine::ProcessKeyEvent()` that Ctrl+key events **do reach Mozc**
   in VSCode — the IBus key event contains `keyval=97 mod=4` (Ctrl+A),
   identical to what Kate sends. The problem is that Mozc returns `false`
   (event not consumed) because `apl_mode_active_` has already been cleared
   by the time the keystroke arrives.

   The mode reset happens during the Enable/FocusIn cycle triggered by
   switching to Mozc (Win+Space) or clicking into a VSCode editor:

   1. `MozcEngine::Enable()` reads the composition mode from `ibus_config_`
      (the IBus XML config). `ConvertCompositionMode()` has no case for APL,
      so it returns `NUM_OF_COMPOSITIONS` and the "Do nothing" branch is
      taken — but `Enable()` also sends `TURN_ON_IME`, which resets the
      session to its default mode (HIRAGANA).
   2. `MozcEngine::FocusIn()` calls `property_handler_->Register()`, which
      rebuilds the IBus property panel using `original_composition_mode_`
      (initialized to `commands::HIRAGANA`).
   3. The session's `apl_mode_active_` flag is cleared by the mode switch,
      so subsequent Ctrl+key events fall through `TryAplShiftedKey()` and
      are passed to the application unhandled.

   This is the same underlying issue as #3 (APL mode reverts after
   autocomplete) — `apl_mode_active_` does not survive state transitions
   triggered by Enable, FocusIn, or preedit commit.

   **Implication**: The fix requires making APL mode persist across
   Enable/FocusIn cycles. Options include: (a) adding APL to
   `ConvertCompositionMode()` and the IBus config so `Enable()` can
   restore it, (b) persisting `apl_mode_active_` in the session so it
   survives `TURN_ON_IME`, or (c) having the property handler track and
   restore APL mode through `Register()` calls. This is a tractable
   fix within the existing architecture — Ctrl as a shifting key is NOT
   fundamentally blocked by Electron/Chromium.

**Pending verification:**

- Ride (Dyalog APL IDE) — to be tested; may have its own key-event handling
  that warrants separate investigation
- macOS — to be tested (see Phase 3)
- Windows — to be tested (see Phase 3)

### Architecture note: HALF_ASCII as the underlying transliteration mode

The APL shifting-key mode sets the Composer's transliteration mode to
`HALF_ASCII` rather than introducing a new `TransliterationType`. This was
evaluated and deemed appropriate because:

- The `TransliterationType` enum is used as array indices, in toggle/cycle
  functions, and throughout the transliteration pipeline. Adding an APL entry
  would be invasive and gain nothing — shifting-key input has no
  composition/transliteration semantics.
- `TryAplShiftedKey()` deliberately bypasses the Composer/Converter pipeline.
  The Composer mode only matters for unshifted keystrokes, where HALF_ASCII
  passthrough is the correct behaviour.
- A session-level `apl_mode_active_` flag tracks APL state independently, and
  `OutputMode()` overrides the reported `CompositionMode` to `APL` so the
  property handler and IBus panel display the correct icon/label.

If a future APL input method requires actual transliteration (e.g. keyword
composition or overstrike), adding a first-class `TransliterationType` should
be reconsidered at that point.

### Early-access distribution milestone

**Goal**: Produce installable packages for Linux, macOS, and Windows containing
only the shifting-key input method (Ctrl+key → APL glyph). These will be
distributed to a small group of testers to surface platform-specific issues as
early as possible.

**Scope**: The installers need not be feature-complete. The minimum bar is:

- Switching to APL mode via the input method menu
- Ctrl+key producing the correct APL glyph
- Stable icon/label in the system tray
- No regressions to normal (non-APL) IME operation

Configurability (choice of shifting key, custom glyph table) is desirable but
not required for the first distribution.

Phase 2: Polish and Configuration
----------------------------------

### Step 2.0: Fix taskbar icon

*Subsumed by Step 2.2c.* Adding APL to `kMozcEngineProperties` gives the
property handler the correct symbol ("⍺") and icon for APL mode. The "_A"
display was caused by the property handler falling through to the HALF_ASCII
entry (symbol "_A") because no APL entry existed.

### Step 2.1: Fix APL mode persistence (full IBus integration)

**Problem**: APL mode does not survive Enable/FocusIn cycles, autocomplete
commits, or focus changes between applications. The `apl_mode_active_` flag
in the session gets cleared whenever a `TURN_ON_IME` or
`SWITCH_COMPOSITION_MODE` command arrives with a non-APL mode. This happens
because the IBus frontend layer has no awareness of APL as a composition mode
— it falls through to defaults (HIRAGANA) during Enable and FocusIn.

**Approach**: Make APL a first-class composition mode throughout the IBus
frontend, so it is preserved and restored by the same mechanisms that already
handle Hiragana, Katakana, etc. The session/composer layer continues to use
HALF_ASCII internally for transliteration (no change needed there — see the
architecture note above).

#### Step 2.1a: Add APL to the IBus config proto

**File**: `src/unix/ibus/ibus_config.proto`

Add `APL = 6` to `Engine.CompositionMode`:

```protobuf
enum CompositionMode {
  DIRECT = 0;
  HIRAGANA = 1;
  FULL_KATAKANA = 2;
  HALF_ASCII = 3;
  FULL_ASCII = 4;
  HALF_KATAKANA = 5;
  APL = 6;        // NEW
  NONE = 100;
}
```

This allows the IBus config to store APL as the engine's composition mode,
so `Enable()` can restore it on focus/switch.

#### Step 2.1b: Add APL to `ConvertCompositionMode()`

**File**: `src/unix/ibus/mozc_engine.cc`

Add the APL case to the switch in `ConvertCompositionMode()`:

```cpp
case ibus::Engine::APL:
  return commands::APL;
```

Without this, `Enable()` sees APL as `NUM_OF_COMPOSITIONS` and takes the
"Do nothing" branch, then falls through to HIRAGANA via subsequent state
transitions.

#### Step 2.1c: Add APL to `kMozcEngineProperties`

**File**: `src/unix/ibus/property_handler.cc`

Add an APL entry to the `kMozcEngineProperties` array:

```cpp
{
    commands::APL,
    "InputMode.APL",
    "APL",
    "⍺",              // panel symbol (APL alpha)
    "apl.png",        // icon (create or reuse existing)
},
```

This gives APL a menu item in the IBus panel, a display symbol ("⍺"), and
ensures `AppendCompositionPropertyToPanel()` and
`UpdateCompositionModeIcon()` handle it correctly. The existing code
iterates `kMozcEngineProperties` to build the radio menu and update the
panel — adding an entry here is sufficient for both.

This also fixes **Issue #1** (taskbar icon shows "_A" instead of APL
glyph), since the property handler will now match `commands::APL` to the
correct entry instead of falling through to the HALF_ASCII entry.

#### Step 2.1d: Ensure session preserves `apl_mode_active_` through `TURN_ON_IME`

**File**: `src/session/session.cc`

There are two code paths that affect `apl_mode_active_`:

**Path 1 — `SWITCH_COMPOSITION_MODE` handler (line ~298)**

```cpp
// Any mode switch clears APL mode; the APL case sets it back.
apl_mode_active_ = false;                              // line 303
switch (session_command.composition_mode()) {
  ...
  case commands::APL:
    result = CompositionModeAPL(command);               // sets apl_mode_active_ = true at line 2181
    break;
  ...
}
```

This is correct — an explicit user mode switch clears APL, and switching
*to* APL sets it back via `CompositionModeAPL()`.

**Path 2 — `MakeSureIMEOn()` (line ~1063), called by `TURN_ON_IME`**

```cpp
bool Session::MakeSureIMEOn(commands::Command* command) {
  ...
  if (command->input().has_command() &&
      command->input().command().has_composition_mode()) {
    ApplyCompositionMode(command->input().command().composition_mode(),
                         context_->mutable_composer());   // line 1078
  }
  OutputMode(command);                                     // line 1081
  return true;
}
```

This is the **problematic path**. When `Enable()` sends `TURN_ON_IME`
with `composition_mode = HIRAGANA`, `ApplyCompositionMode()` switches
the composer to HIRAGANA transliteration — but does NOT touch
`apl_mode_active_`. However, `ApplyCompositionMode` for `commands::APL`
(line ~104) does set the composer to HALF_ASCII but also does NOT set
`apl_mode_active_ = true`. And `OutputMode()` reads the composer's
transliteration state (now HIRAGANA), overriding the APL status.

**The fix**: Add APL awareness to `MakeSureIMEOn()`. When
`composition_mode = APL`, set `apl_mode_active_ = true` and route to
HALF_ASCII (mirroring what `CompositionModeAPL` does). When the mode is
anything other than APL, set `apl_mode_active_ = false`. Alternatively,
make `MakeSureIMEOn()` delegate to the same `CompositionModeAPL()` path
used by `SWITCH_COMPOSITION_MODE`.

Also check `ApplyCompositionMode()` (line ~86) — this free function
handles the APL case at line ~104 by routing to HALF_ASCII, but it
cannot set `apl_mode_active_` because it only has access to the
`Composer`, not the `Session`. Either:
- Move the `apl_mode_active_` management into every caller of
  `ApplyCompositionMode()`, or
- Change `ApplyCompositionMode()` to return a signal that the caller
  should set `apl_mode_active_`.

This fixes **Issue #3** (APL mode reverts after autocomplete) and
**Issue #4** (APL mode lost on Enable/FocusIn).

**Cleanup note**: Remove the temporary file-based key logger added at
`src/unix/ibus/mozc_engine.cc` line 378 (`fopen("/tmp/mozc_keylog.txt")`).
This was added during debugging and must not ship.

#### Step 2.1e: Ensure `OutputMode()` reports APL to the property handler

Already implemented in Phase 1 — `OutputMode()` overrides the reported
`CompositionMode` to `APL` when `apl_mode_active_` is true. With
Step 2.1c in place, the property handler will now correctly match this
to the APL entry and display "⍺" in the panel.

Verify that `property_handler_->Update()` (called from `Enable()` and
key event handlers) correctly propagates the APL mode through
`original_composition_mode_` so it survives subsequent `Register()` calls
during FocusIn.

#### Files changed in Step 2.1

| File | Change |
|------|--------|
| `src/unix/ibus/ibus_config.proto` | Add `APL = 6` to `Engine.CompositionMode` |
| `src/unix/ibus/mozc_engine.cc` | Add APL case to `ConvertCompositionMode()` |
| `src/unix/ibus/property_handler.cc` | Add APL entry to `kMozcEngineProperties` |
| `src/session/session.cc` | Handle APL in `TURN_ON_IME`; audit `apl_mode_active_ = false` sites |

#### Expected result

After this change:
- Switching to Mozc (Win+Space) preserves APL mode if it was active
- Clicking between applications (FocusIn/FocusOut) preserves APL mode
- Pressing Enter, committing preedit, or dismissing autocomplete does not
  clear APL mode
- The taskbar icon shows "⍺" when APL mode is active
- Ctrl+key produces APL glyphs in VSCode and all other applications

### Step 2.2: Suppress autocomplete in APL mode

**Files**: `src/session/session.cc` and/or `src/converter/converter.cc`

While `apl_mode_active_` is true, Mozc should not display the candidate/
suggestion window for unshifted keystrokes. Options:

- Set the `Request` to disable suggestion/prediction when entering APL mode.
- In the state handlers, skip the conversion step and emit a direct result for
  unshifted keys in APL mode (treating them as immediate HALF_ASCII commit).
- Filter candidates in the output before returning to the client.

### Step 2.3: Configurable shifting key

Add a config option (`protocol/config.proto`) to select the APL shifting key:

```protobuf
enum AplShiftingKey {
  APL_SHIFT_CTRL = 0;
  APL_SHIFT_ALT = 1;
  APL_SHIFT_RIGHT_ALT = 2;  // AltGr
}
optional AplShiftingKey apl_shifting_key = N [default = APL_SHIFT_CTRL];
```

Update `TryAplShiftedKey()` to check the configured modifier instead of
hardcoding Ctrl.

### Step 2.4: Ctrl+Shift for shifted APL glyphs

Many APL keyboards have a second layer accessed via Ctrl+Shift (or APL+Shift).
Add a second lookup table for shifted glyphs:

```
Ctrl+Shift+A → ⍶ (alpha underbar)
Ctrl+Shift+W → ⍹ (omega underbar)
...
```

### Step 2.5: Complete the glyph table

Fill out the full APL keyboard layout covering all standard glyphs. Reference
layouts: Dyalog US, GNU APL, IBM APL2.

### Step 2.6: Externalize the glyph table

Move the glyph mapping from C++ code to a data file
(e.g., `src/data/apl/apl_keyboard.tsv`) so it can be edited without
recompilation. Format:

```
# modifier  key  glyph  name
ctrl	a	⍺	alpha
ctrl	w	⍵	omega
ctrl+shift	a	⍶	alpha underbar
...
```

Load this in `Table` or a new `AplTable` class at startup.

### Step 2.7: Handle preedit interaction

When the user presses Ctrl+A while there is uncommitted preedit text:
- Option A: Commit preedit first, then insert APL glyph
- Option B: Cancel preedit, insert APL glyph
- Option C: Insert APL glyph into preedit

Option A is most intuitive. Implement by calling `CommitPreedit()` before
inserting the APL result.

### Step 2.8: Visual feedback

When in APL mode, show "APL" in the language bar with a distinctive icon.
Consider showing the APL keyboard layout as a tooltip or floating window.

Phase 3: Cross-Platform Support
--------------------------------

### Step 3.0: Ride (Dyalog APL IDE) verification

Ride is the primary IDE for Dyalog APL and a key target environment for this
feature. Ride may have its own key-event interception or virtual-keyboard layer
that warrants investigation separately from generic Linux apps. Test the full
Ctrl+key layout inside Ride on Linux before proceeding to other platforms.

### Step 3.1: Windows (TSF/IMM32)

**Key integration points:**

- `src/win32/base/conversion_mode_util.cc`: Add mapping between `commands::APL`
  and a Windows-native conversion mode flag. Windows IME uses flags like
  `IME_CMODE_NATIVE`, `IME_CMODE_KATAKANA`, etc. APL would need a custom flag
  or a repurposing of an unused flag bit.

- `src/win32/tip/` (TSF — Text Services Framework): The modern Windows IME
  interface. Add APL mode to the language bar compartment. TSF exposes mode
  switching via `ITfCompartmentMgr`.

- `src/win32/ime/` (IMM32 — legacy): Add APL support to the older IME
  interface for backward compatibility.

- **Key event flow on Windows**: Windows sends `WM_KEYDOWN`/`WM_KEYUP` with
  virtual key codes. The Mozc Windows client converts these to `KeyEvent`
  protobufs, same as IBus. The session-level `TryAplShiftedKey()` works
  unchanged — only the UI integration (language bar, mode switching) differs.

**Windows-specific challenges:**
- TSF language bar API is different from IBus properties
- Need to register APL mode in the Windows language bar
- Ctrl+key may be intercepted by some Windows applications before reaching the IME
- Need to test with both TSF-aware and IMM32-legacy applications

### Step 3.2: macOS (InputMethodKit)

**Key integration points:**

- `src/mac/mozc_imk_input_controller.mm`: The main InputMethodKit controller.
  Add APL mode handling to the `inputText:key:modifiers:client:` method (or
  equivalent key event handler).

- `src/mac/KeyCodeMap.mm`: Maps macOS virtual key codes to Mozc keycodes.
  No changes needed — the mapping already handles modifier keys.

- **macOS menu bar**: InputMethodKit provides `IMKInputController` methods for
  exposing mode menus. Add APL as a menu item alongside the existing
  Hiragana/Katakana options. This is done via the `menu` method override.

- **macOS-specific challenges:**
  - macOS may intercept Ctrl+key for system shortcuts (Ctrl+Space for Spotlight,
    etc.). AltGr doesn't exist on Mac — Option (Alt) key is the closest analog.
  - The Option key on Mac is commonly used for special character input (e.g.,
    Option+P → π). This could conflict with APL assignments.
  - Recommendation: Use Ctrl on Mac as default, with Option as configurable
    alternative.

### Step 3.3: Shared session layer

The key advantage of our approach: `TryAplShiftedKey()` lives in the
platform-independent session layer. It works identically on all platforms.
Only the UI integration (menus, language bar, icons) is platform-specific.

```
            ┌─────────────────┐
            │  Session Layer  │  ← TryAplShiftedKey() — shared across platforms
            │  (apl_keymap)   │
            └───────┬─────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
   ┌─────────┐ ┌─────────┐ ┌─────────┐
   │  IBus   │ │   TSF   │ │   IMK   │
   │ (Linux) │ │ (Win32) │ │ (macOS) │
   └─────────┘ └─────────┘ └─────────┘
   Menu item    Language     Input menu
   in panel     bar button   in menu bar
```

Phase 4: Advanced Features (Stretch Goals)
-------------------------------------------

### Step 4.1: Keyword search

Allow the user to type a keyword (e.g., "reverse") and see matching APL glyphs
as candidates. Implementation path:

- Add APL symbol entries to the Mozc dictionary with keyword readings
- Or create a dedicated APL rewriter (`src/rewriter/apl_symbol_rewriter.cc`)
  that recognizes keyword patterns and injects APL candidates

### Step 4.2: Idiom search (APLCart-style)

Integrate an APL idiom database. When the user types a description like
"remove duplicates", show the APL idiom `∪` or `{⍵[⍋⍵]}` as candidates.

### Step 4.3: Glyph composition / overstrike

Support composing glyphs by combining base characters:
- Tab completion mode: type partial glyph name, press Tab to complete
- Overstrike mode: ○ combined with | → ⌽ (rotate)

This would require extending the composer with APL-specific composition rules.

### Step 4.4: On-screen keyboard overlay

Show a floating window displaying the APL keyboard layout, updating in
real-time as modifier keys are pressed/released. This is a renderer feature
(`src/renderer/`).

---

Summary of Phases
-----------------

| Phase | Scope | Platform | Status | Key Deliverable |
|-------|-------|----------|--------|-----------------|
| 1 | Minimal POC | Linux | **Done** (2026-02-15) | Ctrl+key → APL glyph via IBus/Wayland/KDE |
| 2 | Polish | Linux | In progress | Icon fix, autocomplete suppression, mode persistence, full layout, config |
| 3 | Cross-platform | Win/Mac/Ride | Pending | TSF + IMK integration, Ride verification |
| 4 | Advanced | All | Pending | Keyword search, idioms, composition, overlay |
