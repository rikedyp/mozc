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
  // Dyalog-style APL keyboard layout (Ctrl+key) — US ANSI positions.
  // See src/session/apl_keymap.cc for the full authoritative table.
  static const auto* kMap = new absl::flat_hash_map<uint32_t, absl::string_view>({
    {'`', "⋄"},  // diamond
    {'1', "¨"},  {'2', "¯"},  {'3', "<"},  {'4', "≤"},
    {'5', "="},  {'6', "≥"},  {'7', ">"},  {'8', "≠"},
    {'9', "∨"},  {'0', "∧"},  {'-', "×"},  {'=', "÷"},
    {'q', "?"},  {'w', "⍵"},  {'e', "∊"},  {'r', "⍴"},
    {'t', "∼"},  {'y', "↑"},  {'u', "↓"},  {'i', "⍳"},
    {'o', "○"},  {'p', "⋆"},  {'[', "←"},  {']', "→"},  {'\\', "⊢"},
    {'a', "⍺"},  {'s', "⌈"},  {'d', "⌊"},  {'f', "_"},
    {'g', "∇"},  {'h', "∆"},  {'j', "∘"},  {'k', "'"},
    {'l', "⎕"},  {';', "⍎"},  {'\'', "⍕"},
    {'z', "⊂"},  {'x', "⊃"},  {'v', "∪"},  {'b', "⊥"},
    {'n', "⊤"},  {'m', "∣"},  {',', "⍝"},  {'.', "⍀"},  {'/', "⌿"},
    // 'c' is unassigned
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

4. **Ctrl+key input does not work in VSCode (Electron/Chromium apps) — on X11.**
   APL glyphs are produced correctly in native applications (e.g. Kate) but
   not in VSCode when running in an X11 or XWayland session. Clicking into a
   VSCode editor also causes the Mozc mode indicator to revert to Latin.

   **Initial hypothesis (INCORRECT)**: Electron (Chromium) intercepts
   Ctrl+key combinations at the application level before they reach IBus.

   **Actual root cause (X11 session)**: Verified via file-based logging in
   `MozcEngine::ProcessKeyEvent()` that Ctrl+key events **do reach Mozc**
   in VSCode — the IBus key event contains `keyval=97 mod=4` (Ctrl+A),
   identical to what Kate sends. The problem is that Mozc returns `false`
   (event not consumed) because `apl_mode_active_` has already been cleared
   by the time the keystroke arrives. (See Step 2.1 for the fix.)

   **Resolved on Wayland (2026-02-18)**: On a native Wayland session
   (`zwp_input_method_v2`), the compositor routes key events through the IME
   before Electron's accelerator table. Ctrl+key APL input works correctly in
   VSCode on Wayland with no additional code changes. The X11/XWayland case
   is a documented limitation (see `ctrl_key_wip.md`).

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

### Step 2.0: Fix taskbar icon — **Verified ✓**

*Subsumed by Step 2.1c.* Adding APL to `kMozcEngineProperties` gives the
property handler the correct symbol ("⍺") and icon for APL mode. The "_A"
display was caused by the property handler falling through to the HALF_ASCII
entry (symbol "_A") because no APL entry existed.

### Step 2.1: Fix APL mode persistence (full IBus integration) — **Verified ✓**

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

#### Result — Verified ✓

- Switching to Mozc (Win+Space) preserves APL mode if it was active
- Clicking between applications (FocusIn/FocusOut) preserves APL mode
- Pressing Enter, committing preedit, or dismissing autocomplete does not
  clear APL mode
- The taskbar icon shows "⍺" when APL mode is active
- Ctrl+key produces APL glyphs in VSCode and all other applications

### Step 2.2: Suppress autocomplete in APL mode — **Verified ✓**

**File**: `src/session/session.cc`

Extended `TryAplShiftedKey()` to handle unshifted printable ASCII keys
(0x20–0x7e) in addition to Ctrl+key combinations. When `apl_mode_active_`
is true and the key carries no Ctrl modifier, the character (`key_string`
if set, otherwise `key_code` cast to char) is committed directly —
bypassing the Composer, `Suggest()`, and the entire conversion pipeline.
Special keys (Backspace, Enter, arrows) are not intercepted because they
use `special_key` rather than `key_code` and fall through to the normal
keymap handler unchanged.

Also removed the Phase 1 diagnostic `LOG(INFO)` noise from
`TryAplShiftedKey()`.

### Step 2.3: Ctrl+Shift for shifted APL glyphs

Many APL keyboards have a second layer accessed via Ctrl+Shift (or APL+Shift).
Add a second lookup table for shifted glyphs and implement the detection in
`TryAplShiftedKey()`:

```
Ctrl+Shift+A → ⍶ (alpha underbar)
Ctrl+Shift+W → ⍹ (omega underbar)
...
```

**Files**: `src/session/apl_keymap.h`, `src/session/apl_keymap.cc`,
`src/session/session.cc`

Add `GetAplShiftedGlyph(uint32_t key_code)` alongside `GetAplGlyph()`.
In `TryAplShiftedKey()`, when Ctrl is held, check for SHIFT in the modifier
list first and look up the shifted table; fall back to the unshifted table if
no shifted glyph is defined for that key.

### Step 2.4: Configurable shifting key — **Verified ✓ (Ctrl and Alt only)**

Added `AplShiftingKey` to `protocol/config.proto` (`APL_SHIFT_CTRL = 0`,
`APL_SHIFT_ALT = 1`) and exposed the choice as a dropdown in the properties
dialog. `TryAplShiftedKey()` reads the server-side config via
`context_->GetConfig()` (not `command->input().config()`, which is empty for
normal key events) and checks the appropriate modifier.

**AltGr is not supported in the POC.** See below for the rationale and the
approach required for a proper future implementation.

#### How AltGr is implemented — **Verified ✓ (pending build)**

AltGr (Right Alt) is handled as ISO Level 3 Shift by the X11 keyboard layer,
*before* IBus receives the event. When the user presses `AltGr+a`, X11
applies the level-3 mapping and delivers the composed character (e.g. `æ` on
many European layouts) to IBus — not an `(AltGr, a)` pair.

Two obstacles had to be overcome:

1. **`key_event_handler.cc` discards the event**: `GetKeyEvent()` filters out
   events with `IBUS_MOD5_MASK` set (the mask for AltGr / ISO Level 3 Shift).
   The filter was added defensively alongside the Super+Space fix (issue #853),
   but the actual Super+Space problem uses `IBUS_MOD4_MASK` (Super), not MOD5.
   Removing MOD5 from the filter would cause regressions on US layouts (where
   AltGr doesn't compose, so the keyval passes `IsAscii()` and would be
   consumed as a plain character without any recognised modifier flag). The
   solution is to intercept MOD5 events in `mozc_engine.cc` *before*
   `GetKeyEvent()` is called — gated on APL mode + `right_alt` configured.
   The filter in `key_event_handler.cc` is not changed.

2. **The keyval is the composed character, not the base key**: on European
   layouts `keyval` is already `æ`, not `a`. The solution is to use the raw
   Linux evdev scancode (`keycode` parameter, physical key position,
   layout-independent — IBus passes evdev scancodes, not X11 keycodes) together
   with a hardcoded US QWERTY scancode→ASCII table.
   APL keyboard layouts are defined by physical position (QWERTY-based), so
   keycode 38 → `'a'` is correct regardless of whether the user's layout
   labels that key `'a'`, `'q'` (AZERTY), or anything else.

**Implementation** (`src/unix/ibus/mozc_engine.cc`):

```cpp
// In ProcessKeyEvent, before GetKeyEvent:
if ((modifiers & IBUS_MOD5_MASK) && !(modifiers & IBUS_RELEASE_MASK) &&
    apl_shifting_key_set_.right_alt() &&
    property_handler_->GetOriginalCompositionMode() == commands::APL) {
  std::optional<AplKeyChars> chars = AplKeycodeToChars(keycode);
  if (chars.has_value()) {
    const bool is_shifted = (modifiers & IBUS_SHIFT_MASK) != 0;
    const uint32_t kc = is_shifted ? chars->shifted : chars->unshifted;
    std::optional<absl::string_view> glyph = session::GetAplShiftedGlyph(kc);
    if (!glyph.has_value()) glyph = session::GetAplGlyph(kc);
    if (glyph.has_value()) {
      engine->CommitText(*glyph);
      return true;
    }
  }
  return false;
}
```

`AplKeycodeToChars()` is a file-scope function in `mozc_engine.cc`'s anonymous
namespace, mapping X11 keycodes to `{unshifted, shifted}` ASCII char pairs.
`engine->CommitText()` commits the glyph directly, bypassing
`GetKeyEvent`/`SendKeyWithContext` (correct since there is no preedit or
session state to update for a direct-commit glyph in APL mode).

Caps Lock with `level3(caps_switch)` XKB option active also generates
`IBUS_MOD5_MASK` events and is handled by this same path at no extra cost.

**Files changed**:

| File | Change |
|------|--------|
| `src/unix/ibus/mozc_engine.cc` | Add `AplKeycodeToChars()` + MOD5 intercept in `ProcessKeyEvent` |
| `src/unix/ibus/BUILD.bazel` | Add `//session:apl_keymap` dep |

#### Super (Windows key) — not yet supported; investigation needed

**TODO**: Investigate whether Super (Windows/Meta key, `IBUS_MOD4_MASK`) can
be supported as an APL shifting key on Linux.

Current blockers:

1. **Blanket filter in `key_event_handler.cc`**: `GetKeyEvent()` discards any
   event with `IBUS_MOD3_MASK | IBUS_MOD4_MASK | IBUS_MOD5_MASK` set. This
   filter was added to avoid conflicting with Super+Space (the IBus
   input-method switcher shortcut). Enabling Super requires either making this
   filter config-aware (skip the filter when Super is the configured shifting
   key) or verifying that Super+character events are not intercepted by the
   desktop environment before reaching IBus.

2. **Super is not in `commands::KeyEvent::ModifierKey`**: A new enum value
   (e.g. `SUPER = 8192`) would be needed, along with detection of
   `IBUS_MOD4_MASK` in `key_translator.cc` and `key_event_handler.cc`.

3. **Desktop environment grabbing**: GNOME and KDE bind many Super+key
   combinations to system shortcuts. The practical set of Super+character
   combinations that survive to IBus may be very small. Empirical testing
   across GNOME, KDE, and i3/Sway is needed before committing to this option.

Suggested investigation steps:
- On a KDE/Wayland session, check which Super+letter events actually reach
  IBus by temporarily logging all events in `MozcEngine::ProcessKeyEvent()`.
- Determine if removing `IBUS_MOD4_MASK` from `kExtraModMask` (or doing so
  conditionally) breaks Super+Space or any other system shortcuts in practice.
- If viable, add `SUPER = 8192` to `ModifierKey`, emit it from the translator,
  and add `APL_SHIFT_SUPER = 4` to `AplShiftingKey`.

### Step 2.4a: Fix Ctrl+key shortcuts broken when Alt is the shifting key — **Verified ✓**

**Root cause**: The unshifted-passthrough path added in Step 2.2
committed any printable-range `key_code` without first checking whether a
non-shifting modifier was held. Execution trace for Ctrl+A when Alt is the
shifting key:

```
TryAplShiftedKey():
  shifting_modifier = ALT           (config = APL_SHIFT_ALT)
  modifier_keys = [CTRL]            (Ctrl+A event)
  has_shifting_key = false          ALT not in modifiers → falls through
  kc = 0x61 ('a')                   in range 0x20–0x7e ✓
  commits 'a', returns true         BUG: Ctrl+A consumed by IME
```

The function checked for the configured shifting key, found it absent, then
fell straight into the unshifted passthrough which committed the bare
character and returned `true` (consumed). Ctrl+A never reached the
application.

**Fix** (`src/session/session.cc`): Added a guard at the top of the
unshifted-passthrough block that iterates `modifier_keys` and returns
`false` (pass through) if any modifier other than Shift / Left Shift /
Right Shift / Caps is present. Shift and Caps are permitted because their
effect is already reflected in `key_string` (e.g. Shift+A gives `key_string
= "A"`). Any other modifier (Ctrl, Alt, Left-Alt, Right-Alt, …) indicates
an application shortcut that the IME must not consume.

This is safe because the `has_shifting_key` block above already handles the
case where the configured shifting key is held — by the time we reach the
unshifted path, the configured shifting modifier cannot be in the list.

**Expected behaviour after fix**: With Alt as the shifting key:
- Alt+A → ⍺ (consumed by IME — shifting-key path)
- A → 'a' (consumed by IME — unshifted passthrough, no modifier)
- Ctrl+A → **not consumed** → application receives Ctrl+A normally
- Ctrl+Shift+A → **not consumed** → application receives Ctrl+Shift+A normally

**Files**: `src/session/session.cc`

#### Step 2.4b: Fix menu-bar focus activation when Alt is the shifting key — **Verified ✓**

**Root cause**: When the IME consumes Alt+A (APL glyph committed), the app's
X11 event stream looks like this:

```
Alt_L down  → ProcessKeyEvent returns false → forwarded to app
'a'   down  → ProcessKeyEvent returns true  → consumed (APL glyph)
'a'   up    → ProcessKeyEvent returns false → forwarded to app
Alt_L up    → ProcessKeyEvent returns false → forwarded to app
```

The app (VSCode/Electron, GTK) tracks whether a non-modifier key was pressed
while Alt was held. Since `'a'` down was consumed and never forwarded, the
app sees: Alt-down, Alt-up, no non-modifier key in between → interprets as
"Alt tapped" → focuses the menu bar.

**Why suppressing only Alt-up is insufficient**: the app would see a
permanent Alt-stuck-down state (Alt-down forwarded, but Alt-up suppressed).

**Fix** (`src/unix/ibus/mozc_engine.cc`, `src/unix/ibus/mozc_engine.h`):
Cache the `apl_shifting_key` config field in `MozcEngine::UpdatePreeditMethod()`
alongside `preedit_method_`. In `ProcessKeyEvent`, add an early return
that consumes bare `Alt_L`/`Alt_R`/`Meta_L`/`Meta_R` key events when:

1. Alt is the configured APL shifting key (`APL_SHIFT_ALT`), and
2. APL mode is currently active (`GetOriginalCompositionMode() == APL`)

This is a deliberate trade-off: in APL mode with Alt as the shifting key,
tapping Alt to open the menu bar is disabled. This is acceptable because the
user has chosen to dedicate Alt to APL input; using Alt for menu navigation
simultaneously is not a supported use case.

**Files**: `src/unix/ibus/mozc_engine.cc`, `src/unix/ibus/mozc_engine.h`

### Step 2.5: Remove Japanese modes for early-access release

For the early-access POC, hide all Japanese composition modes from the IBus
property menu so the UI presents only APL and Direct input. The Japanese
converter/dictionary code remains in the binary (removing it is invasive and
risks regressions), but no Japanese modes are exposed to the user.

**File**: `src/unix/ibus/property_handler.cc`

Trim `kMozcEngineProperties` to retain only the DIRECT and APL entries.
Remove the Hiragana, Full Katakana, Half ASCII, Full ASCII, and Half Katakana
entries from the array.

**File**: `src/unix/ibus/mozc_engine.cc`

Change the default/fallback composition mode (used when the engine first
starts and no saved mode is found) from HIRAGANA to APL.

### Step 2.6: Verify glyph table correctness

Review the existing APL glyph mappings in `src/session/apl_keymap.cc` against
the Dyalog US keyboard layout to confirm every entry is correct before moving
on to cross-platform work. Flag any misassignments or missing glyphs and
correct them. Reference layouts: Dyalog US, GNU APL, IBM APL2.

### Step 2.7: Externalize the glyph table — *Design investigation (not prototype scope)*

Externalising the glyph mapping to a data file
(e.g., `src/data/apl/apl_keyboard.tsv`) would allow users to customise
layouts without recompiling. However, this adds complexity (file loading,
error handling, packaging) that is not needed for the prototype.

This step is deferred to design investigation. Before implementing, consider:
- Whether runtime configurability is actually required, or whether a
  compile-time rebuild is acceptable for advanced users.
- How the data file would be installed and located at runtime (XDG data dirs,
  bundled resource, etc.).
- Whether the existing Mozc `Table` / preedit-table infrastructure can be
  reused rather than building a new loader.

No code changes in this phase.

### Step 2.8: Handle preedit interaction — *Not needed*

This step was written to handle the case where shifting-key input fires while
uncommitted preedit text is visible. However, Step 2.2 already commits all
printable ASCII keystrokes directly, bypassing the Composer entirely when APL
mode is active. This means the preedit buffer is always empty in APL mode —
there is no uncommitted text to interact with. Step 2.8 is therefore a no-op
and will not be implemented.

### Step 2.9: Visual feedback — **Verified ✓**

The IBus language bar shows "⍺" when APL mode is active (implemented as part
of Step 2.1c). This is sufficient for the prototype. Showing the APL keyboard
layout as a tooltip or floating window is deferred to Phase 4 (Step 4.4).

### Step 2.10: Comprehensive shifting key selection — multi-key checklist

**Goal**: Replace the current two-option dropdown (Ctrl / Alt) with a checklist
of all candidate shifting keys. Users can enable any combination simultaneously.
This is a prerequisite for supporting the full range of keys that XKB-based APL
implementations (GNU APL, Dyalog) traditionally support.

#### Protocol change

Replace the `AplShiftingKey` enum field in `src/protocol/config.proto` with a
structured message:

```protobuf
// src/protocol/config.proto
message AplShiftingKeySet {
  bool left_ctrl   = 1;
  bool right_ctrl  = 2;
  bool left_alt    = 3;
  bool right_alt   = 4;   // AltGr / ISO Level 3 Shift
  bool left_super  = 5;   // Left Windows key
  bool right_super = 6;   // Right Windows key
  bool left_shift  = 7;
  bool right_shift = 8;
  bool caps_lock   = 9;   // latch — CAPS flag in modifier state
  bool menu_key    = 10;  // App/Menu key — latch/toggle only
}
```

Default: `left_ctrl = true, right_ctrl = true` (matches current behaviour).
Remove or deprecate the existing `AplShiftingKey` enum. `TryAplShiftedKey()` and
`MozcEngine::UpdatePreeditMethod()` are updated to read the new message.

#### Qt config dialog change

In `src/gui/config_dialog/`, replace the APL shifting key dropdown with a checkbox
group, arranged in keyboard order:

```
APL Shifting Keys
┌──────────────────────────────────────────────────┐
│ [ ] Left Shift        [ ] Right Shift             │
│ [✓] Left Ctrl         [✓] Right Ctrl              │
│ [ ] Left Alt          [ ] Right Alt (AltGr)  ⚠   │
│ [ ] Left Windows  ⚠   [ ] Right Windows  ⚠        │
│ [ ] Caps Lock  ★      [ ] Menu Key  ★             │
└──────────────────────────────────────────────────┘
  ⚠ May require additional system configuration — see Help
  ★ Latch/toggle mode (press once to activate; not a hold-down key)
```

#### Per-key investigation summary

| Key | `ModifierKey` flag | Current status | Estimated effort |
|-----|--------------------|----------------|------------------|
| Left Ctrl | `LEFT_CTRL (32)` | **Implemented** — see per-key notes | Done |
| Right Ctrl | `RIGHT_CTRL (256)` | **Implemented** — see per-key notes | Done |
| Left Alt | `LEFT_ALT (64)` | **Implemented** — see per-key notes | Done |
| Right Alt (AltGr) | `RIGHT_ALT (512)` | Blocked — see Step 2.4 | High — MOD5 filter + keycode approach; or XKB workaround (see below) |
| Left Windows | Not in enum (MOD4) | Blocked — see Step 2.4 | Medium — add `SUPER_L` to enum + empirical investigation |
| Right Windows | Not in enum (MOD4) | Blocked — see Step 2.4 | Medium — same as Left Windows |
| Left Shift | `LEFT_SHIFT (128)` | Emitted by key_event_handler; not in AplShiftingKeySet yet | Low (with caveats — see note) |
| Right Shift | `RIGHT_SHIFT (1024)` | Emitted by key_event_handler; not in AplShiftingKeySet yet | Low (with caveats — see note) |
| Caps Lock | `CAPS (2048)` | Not yet implemented | Low — CAPS flag already available in the protocol |
| Menu Key | No modifier flag | Not yet implemented | Medium — latch mechanism differs from hold-down keys |

#### Per-key notes

**Left Ctrl / Right Ctrl (independent selection) — Verified ✓**

**Investigation result**: `key_translator.cc` only emits the combined `CTRL` flag
(from `IBUS_CONTROL_MASK`) — it does NOT emit `LEFT_CTRL` or `RIGHT_CTRL`. However,
`key_event_handler.cc` already maintains `currently_pressed_modifiers_` as a set of
IBus keyvals (e.g., `IBUS_Control_L`, `IBUS_Control_R`). This state is the correct
source for L/R distinction.

**Solution**: augment `ProcessModifiers()` in `key_event_handler.cc` to iterate
`currently_pressed_modifiers_` when a non-modifier key is pressed and emit
`LEFT_CTRL`/`RIGHT_CTRL`/`LEFT_ALT`/`RIGHT_ALT`/`LEFT_SHIFT`/`RIGHT_SHIFT`
alongside the combined flags already set by `key_translator`. `TryAplShiftedKey()`
then checks the specific flags when the new `AplShiftingKeySet` config is used.

When both sides are configured (the default), the combined `CTRL` flag is also
accepted as a fallback in case L/R state was lost due to a focus change.

**Left Alt / Right Alt (independent selection) — Verified ✓**

Same mechanism as Ctrl: `key_event_handler.cc` emits `LEFT_ALT`/`RIGHT_ALT` based
on `currently_pressed_modifiers_`. Left Alt has no XKB complications. Right Alt
(AltGr) remains blocked by the MOD5 filter — see Step 2.4 for the full analysis.

**Bug fixed**: `LEFT_ALT`/`RIGHT_ALT` were never actually reaching `TryAplShiftedKey()`
for left Alt. Root cause: the bare Alt key-down is consumed early in `ProcessKeyEvent`
(menu-bar suppression, commit `132b82d47`) before `GetKeyEvent()` runs, so
`currently_pressed_modifiers_` never records `IBUS_Alt_L`. Fix: track
`left_alt_held_`/`right_alt_held_` in `MozcEngine` and inject `LEFT_ALT`/`RIGHT_ALT`
into the `KeyEvent` after `GetKeyEvent()` returns, guarded by `IBUS_MOD1_MASK` and APL
mode. Same pattern as the existing `left_super_held_`/`right_super_held_` injection.

**Left Shift / Right Shift**

Using Shift as the APL shifting key means Shift+A → ⍺ rather than 'A'. This matches
the "upper APL layer" on physical APL keyboard overlays. Keys without an APL shifted
assignment must fall through to normal behaviour (capital, punctuation), so
`TryAplShiftedKey()` must return `false` when no glyph is found rather than
suppressing the keystroke. **Caveat**: the unshifted-passthrough guard (Step 2.4a)
currently passes any event with no non-shift modifiers through directly; when Shift
itself is the configured shifting key, this guard must be updated so that Shift+key
triggers the APL glyph lookup instead.

**Caps Lock (latch mode)**

`commands::KeyEvent::CAPS (2048)` is already in the `ModifierKey` enum and reflects
the Caps Lock *state* (LED on/off), not a held key. Checking for `CAPS` in the
modifier list inside `TryAplShiftedKey()` activates APL shifting with **no XKB
changes required**. The IBus layer already delivers the CAPS flag on every keystroke
when Caps Lock is on.

The Caps Lock key-press keysym (`IBUS_Caps_Lock`) arrives as a separate event.
Verify whether it should be consumed by the IME (preventing a double-toggle if the
user presses Caps Lock while already in APL mode) or passed through unchanged.

**Menu Key (App Key — latch/toggle)**

The Menu/App key generates keysym `IBUS_Menu` (0xFF67) with no modifier flags. It
cannot be held down, so it cannot function as a hold-down shifting modifier. Instead
it acts as a **mode-toggle latch**: first press activates APL mode; next press
deactivates it (equivalent to switching modes in the IBus panel). This is a separate
intercept path — detect the `IBUS_Menu` keysym in the key-event handler and dispatch
a `SWITCH_COMPOSITION_MODE` command. Independent of the glyph lookup path;
`TryAplShiftedKey()` is not involved.

**Left Windows / Right Windows (Super)**

Blocked by (1) the `kExtraModMask` filter in `key_event_handler.cc` discarding
`IBUS_MOD4_MASK` events, and (2) desktop-environment grabbing of most Super+key
combinations before they reach IBus. Investigation steps are documented in Step 2.4.
If the blanket filter is made config-aware, new `SUPER_L` / `SUPER_R` values must be
added to `commands::KeyEvent::ModifierKey` and emitted from `key_translator.cc`.

#### XKB hybrid approach — a workaround for hard-to-intercept keys

For keys that cannot be cleanly intercepted at the IBus level (AltGr, Super, Caps
Lock, Menu Key), an alternative is to ship a **custom XKB symbols file** alongside
the IBus component. This file defines APL glyphs at the appropriate keyboard level
for the chosen shifting key, operating entirely within the XKB layer. The IBus
component is not involved for glyphs produced this way.

**Installation path**: copy to `/usr/share/X11/xkb/symbols/apl` (system-wide) or
`~/.config/xkb/symbols/apl` (user-space; supported by libxkbcommon ≥ 1.0 and most
modern compositors). Activated via `setxkbmap -layout us+apl` or through GNOME/KDE
keyboard settings.

**How it resolves specific blockers**:

- **AltGr**: Define APL glyphs at ISO Level 3. XKB resolves AltGr+key to the APL
  Unicode codepoint and delivers it to the application directly — no changes to
  `key_event_handler.cc` needed for this shifting-key option.
- **Caps Lock**: XKB can remap Caps Lock to `ISO_Level3_Shift` or a custom modifier
  (`Hyper_L`). Remapping it to ISO Level 3 makes Caps Lock and AltGr
  interchangeable APL shifters at the XKB level.
- **Super**: Some Super+letter combinations survive to XKB before the desktop
  environment claims them (especially on tiling window managers). An XKB symbols
  file can define APL glyphs for the surviving combinations.
- **Menu Key**: Can be remapped to a modifier in XKB (precedent: `compose:menu`
  option); an APL-shift variant could follow the same pattern.

**Relationship to IBus mode**: The XKB approach works independently of whether the
user has selected APL mode in IBus. A combined deployment could use:
- IBus mode for features requiring composition (keyword search, idiom lookup —
  Phase 4) and for Ctrl/Alt shifting where IBus interception already works.
- XKB symbols for raw glyph input via AltGr/Super/Caps Lock, even when IBus APL
  mode is not active, or as a fallback on systems where the IBus intercept is
  unreliable.

**Packaging**: include the XKB symbols file in the Mozc APL package; install via
the post-install script alongside the IBus component. Offer a user-space path
(`~/.config/xkb/`) for users without system package permissions.

**Investigation needed**: determine whether a single XKB symbols file can expose
multiple shifting-key options via XKB options (so users choose at the `setxkbmap`
or GNOME/KDE level), or whether separate files per shifting key are required. Confirm
that `~/.config/xkb/` is honoured by the X server and compositor versions in the
target distributions.

#### IBus panel submenu — menu state sync bug (fixed)

The shifting-key checklist was moved from the Qt config dialog to an IBus panel
submenu (commit `842aee86c`). Each entry is a `PROP_TYPE_TOGGLE` property.

**Bug**: `ProcessPropertyActivate` updated `Config` via `SetConfig` but never called
`prop.SetState()` + `engine->UpdateProperty()` on the toggled item. Consequence:
the IBus property GObject retained its original constructed state indefinitely.
On every `FocusIn`, `RegisterProperties` re-sent those stale states to the panel,
resetting the visual checkmarks to whatever the config was at engine startup —
regardless of what the user had ticked since.

**Fix**: after `SetConfig`, call `prop.SetState(new_value ? PROP_STATE_CHECKED :
PROP_STATE_UNCHECKED)` and `engine->UpdateProperty(&prop)`. This mirrors the
pattern used in `UpdateCompositionModeIcon()` for the mode radio buttons.

#### Files changed

| File | Change |
|------|--------|
| `src/protocol/config.proto` | Replace `AplShiftingKey` enum with `AplShiftingKeySet` message |
| `src/session/session.cc` | Update `TryAplShiftedKey()` to iterate the key set; add Caps Lock CAPS-flag check; add Menu Key latch path |
| `src/session/session.h` | Update cached config type if needed |
| `src/gui/config_dialog/config_dialog.{cc,h}` | Replace dropdown with checkbox group |
| `src/gui/config_dialog/config_dialog.ui` | Add checkbox group widget |
| `src/unix/ibus/mozc_engine.cc` | Cache `AplShiftingKeySet` instead of single enum in `UpdatePreeditMethod()`; add Menu Key intercept |
| `data/xkb/apl` | New: XKB symbols file (APL glyphs at configured level; see XKB hybrid note) |
| `install.sh` | Install XKB symbols file to system or user path |

### Step 2.11: Transparent XKB configuration from the Qt dialog

**Goal**: When the user ticks an XKB-backed shifting key (AltGr, Super, or Caps Lock
via XKB remapping) in the Qt checklist, the IME applies the XKB configuration
automatically — detecting the desktop environment and using the appropriate mechanism.
Unticking reverts the change. No manual `setxkbmap` or config-file editing is
required. The user sees one unified interface regardless of what happens underneath.

#### Which keys need XKB management

Some keys can be handled entirely within IBus (no XKB involvement); others require
XKB changes. This determines which checkboxes trigger XKB management on Apply.

| Key | Mechanism | XKB management needed |
|-----|-----------|-----------------------|
| Left/Right Ctrl | IBus (`LEFT_CTRL` / `RIGHT_CTRL` flags) | No |
| Left/Right Alt | IBus (`LEFT_ALT` / `RIGHT_ALT` flags) | No |
| Left/Right Shift | IBus (`LEFT_SHIFT` / `RIGHT_SHIFT` flags) | No |
| Menu Key | IBus (keysym `IBUS_Menu`) | No |
| Caps Lock | IBus (`CAPS` state flag) — preferred path | No (IBus path) |
| Caps Lock via XKB | XKB remap to `ISO_Level3_Shift` | Yes (XKB path, optional alternative) |
| Right Alt (AltGr) | XKB Level 3 — preferred path | Yes |
| Left/Right Super | XKB (if viable) | Yes |

The IBus path for Caps Lock (checking the `CAPS` flag — Step 2.10) works without
any XKB changes and should be the default. The XKB path (remapping Caps Lock to a
modifier) is an alternative for users who find the IBus path unreliable, or who want
the XKB and IBus behaviours consistent.

#### Desktop environment and session detection

Detect at runtime using environment variables, before attempting any XKB operation:

```cpp
// src/gui/config_dialog/xkb_configurator.cc
enum class SessionType  { X11, Wayland, Unknown };
enum class DesktopEnv   { GNOME, KDE, Sway, Other };

SessionType DetectSession() {
  const char* t = getenv("XDG_SESSION_TYPE");
  if (!t) return SessionType::Unknown;
  if (absl::EqualsIgnoreCase(t, "wayland")) return SessionType::Wayland;
  return SessionType::X11;
}

DesktopEnv DetectDesktop() {
  const char* d = getenv("XDG_CURRENT_DESKTOP");
  if (!d) return DesktopEnv::Other;
  std::string s(d);
  if (s.find("GNOME") != std::string::npos) return DesktopEnv::GNOME;
  if (s.find("KDE")   != std::string::npos) return DesktopEnv::KDE;
  if (s.find("sway")  != std::string::npos ||
      s.find("Hyprland") != std::string::npos) return DesktopEnv::Sway;
  return DesktopEnv::Other;
}
```

Detection matrix:

| Session | Desktop | Configuration method | Persistent? |
|---------|---------|----------------------|-------------|
| `x11` | any | `setxkbmap -option apl:altgr` (subprocess) | No — also write autostart (see below) |
| `wayland` | GNOME | `gsettings` (subprocess or GLib API) | Yes |
| `wayland` | KDE | `kwriteconfig5`/`6` + DBus reload | Yes |
| `wayland` | Sway/Hyprland | `swaymsg`/`hyprctl input` | No — also write include file |
| `wayland` | other/unknown | Show manual-instructions dialog | n/a |

#### XKB symbols file structure

The symbols file uses named variants so that different shifting keys map to distinct
XKB option strings. All variants share the same glyph table; only the modifier
binding differs.

```
// data/xkb/apl
// Installed to /usr/share/X11/xkb/symbols/apl (system)
// or ~/.config/xkb/symbols/apl (user-space, libxkbcommon ≥ 1.0)

partial alphanumeric_keys
xkb_symbols "altgr" {
    // Right Alt (AltGr) activates APL glyphs at Level 3
    include "level3(ralt_switch)"
    key <AD01> { [ q, Q, 0x1000FD3 ] };  // ⍳  (APL iota)
    key <AD02> { [ w, W, 0x1000279 ] };  // ⍹  (APL omega underbar)
    // ... full layout (Unicode codepoints for all APL glyphs)
};

partial alphanumeric_keys modifier_keys
xkb_symbols "caps" {
    // Caps Lock remapped to APL shift (Level 3)
    include "level3(caps_switch)"
    key <AD01> { [ q, Q, 0x1000FD3 ] };
    // ... same glyph assignments
};

partial alphanumeric_keys modifier_keys
xkb_symbols "super_l" {
    // Left Super as APL shift — experimental; see Super investigation note
    include "level3(lwin_switch)"
    key <AD01> { [ q, Q, 0x1000FD3 ] };
    // ...
};
```

The option strings used (`apl:altgr`, `apl:caps`, `apl:super_l`) must be registered
in a rules fragment so that `setxkbmap -option` and GNOME/KDE option pickers can
resolve them. Ship a rules XML fragment:

```xml
<!-- data/xkb/apl.xml — merged into /usr/share/X11/xkb/rules/evdev.xml by install.sh -->
<optionList>
  <group allowMultipleSelection="false">
    <configItem><name>apl</name>
      <description>APL shifting key</description>
    </configItem>
    <option><configItem><name>apl:altgr</name>
      <description>Right Alt (AltGr) as APL shift</description>
    </configItem></option>
    <option><configItem><name>apl:caps</name>
      <description>Caps Lock as APL shift</description>
    </configItem></option>
    <option><configItem><name>apl:super_l</name>
      <description>Left Super (Windows key) as APL shift</description>
    </configItem></option>
  </group>
</optionList>
```

**Investigation needed**: merging into `evdev.xml` without overwriting it requires
careful XML editing in `install.sh`. Alternatively, newer XKB implementations support
drop-in files in `/usr/share/X11/xkb/rules/evdev.d/` — check whether target
distributions support this. For user-space installation (`~/.config/xkb/`), confirm
that the local rules file is honoured by the compositor (verified in libxkbcommon ≥
1.5.0 but not all compositors expose the user XKB path to their compositor-level
rule evaluation).

#### Configuration application — per environment

Applied via a helper class `XkbConfigurator` with a simple interface:

```cpp
// src/gui/config_dialog/xkb_configurator.h
class XkbConfigurator {
 public:
  // Read current active XKB options (excluding any apl:* options).
  // Stored before first Apply so they can be restored on revert.
  static std::string GetCurrentOptions();

  // Add or remove apl:* options and apply.
  // options_to_add: e.g. {"apl:altgr"}
  // Returns true on success; false if the DE is unsupported (caller shows dialog).
  static bool Apply(const std::vector<std::string>& apl_options);

  // Remove all apl:* options and restore the baseline saved by GetCurrentOptions().
  static bool Revert();
};
```

**X11 (any desktop)**:

```sh
# Read current options:
setxkbmap -query  # parse "options:" line

# Apply:
setxkbmap -option ""                     # clear first to avoid accumulation
setxkbmap -option "existing,apl:altgr"  # reapply with APL option added

# Session persistence — write autostart desktop file:
~/.config/autostart/mozc-apl-xkb.desktop
  Exec=setxkbmap -option "existing,apl:altgr"
```

**Wayland + GNOME**:

```sh
# Read:
gsettings get org.gnome.desktop.input-sources xkb-options

# Apply (add apl:altgr to existing list):
gsettings set org.gnome.desktop.input-sources xkb-options \
  "$(gsettings get ... | sed "s/]/, 'apl:altgr']/")"

# Persistent by default (dconf storage).
```

**Wayland + KDE (Plasma 5/6)**:

```sh
# Read ~/.config/kxkbrc [Layout] Options=...
# Write updated value:
kwriteconfig6 --file kxkbrc --group Layout --key Options "existing,apl:altgr"

# Trigger live reload:
dbus-send --session --type=method_call \
  --dest=org.kde.keyboard /Layouts org.kde.KeyboardLayouts.reloadConfig

# Persistent (kxkbrc is read on login).
```

**Wayland + Sway / Hyprland**:

```sh
# Apply for current session (not persistent):
swaymsg input type:keyboard xkb_options "existing,apl:altgr"

# Persistence: write to a dedicated include file:
~/.config/sway/conf.d/mozc-apl-xkb.conf
  input type:keyboard { xkb_options "existing,apl:altgr" }
# Tell user to add `include ~/.config/sway/conf.d/*.conf` to main config
# if not already present.
```

**Wayland + unknown desktop**:

Show a non-modal information dialog:

```
APL shifting key (AltGr) requires a keyboard configuration change.
Your desktop environment was not recognised for automatic configuration.

To enable manually, run:
    setxkbmap -option apl:altgr

To make the change permanent, add the above command to your
session startup script (~/.profile, ~/.xprofile, or equivalent).
```

Mark the checkbox with ⚠ in the UI and keep it enabled so the user can still store
the preference in `config.proto` (the IBus side will still function for keys that
are reachable via IBus; only the XKB-level behaviour is unavailable).

#### Reverting XKB changes

Before the first Apply, `XkbConfigurator::GetCurrentOptions()` saves the baseline
option string (without any `apl:*` entries). On revert (user unticks all XKB-backed
keys, or clicks Cancel after Apply):

1. Remove all `apl:*` options from the saved baseline.
2. Re-apply via the same DE-specific method.
3. Remove or update the autostart/include file.

#### Qt dialog integration

Changes are applied only when the user clicks **OK** or **Apply** in the config
dialog, not on individual checkbox toggles. The dialog's existing Apply handler:

1. Saves `AplShiftingKeySet` to `config.proto` via the normal config write path
   (this covers all IBus-native keys immediately).
2. Computes the set of XKB options implied by the new `AplShiftingKeySet`
   (e.g. `right_alt=true` → `"apl:altgr"`; `left_super=true` → `"apl:super_l"`).
3. Calls `XkbConfigurator::Apply(options)` and shows an error dialog on failure.

#### Files changed

| File | Change |
|------|--------|
| `src/gui/config_dialog/xkb_configurator.{cc,h}` | New: DE detection + per-DE apply/revert logic |
| `src/gui/config_dialog/config_dialog.cc` | Call `XkbConfigurator::Apply()` on Apply/OK; call `Revert()` on Cancel if already applied |
| `src/gui/config_dialog/BUILD.bazel` | Add `xkb_configurator` target |
| `data/xkb/apl` | XKB symbols file with `altgr`, `caps`, `super_l` variants |
| `data/xkb/apl.xml` | XKB rules fragment for option registration |
| `install.sh` | Install XKB files; merge rules fragment into `evdev.xml` or drop into `evdev.d/` |

Phase 3: Cross-Platform Support
--------------------------------

### Step 3.0: Ride (Dyalog APL IDE) verification

Ride is the primary IDE for Dyalog APL and a key target environment for this
feature.

**Known limitation**: Ride uses an older Electron version that runs under
XWayland even in a Wayland session. On XWayland, Chromium/Electron's
accelerator table fires before the IBus D-Bus response arrives, so
`consumed=true` from mozc cannot suppress Ctrl+key shortcuts. Ctrl+A in
Ride triggers "select all" rather than producing ⍺.

**Decision**: Accept this limitation for now. Ride is expected to update its
Electron version before this IME ships, at which point running under a native
Wayland session (which works correctly — verified in VSCode) should resolve it
automatically. If Ride is still on an old Electron at release time, the
Right-Alt shifting key (Step 2.4) is a viable workaround since Right-Alt is
not in Electron's accelerator table.

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
| 2 | Polish | Linux | In progress (2.0–2.5 done, 2.4a–b done, 2.9 done; 2.10 Ctrl/Alt in progress) | Verify glyph table (2.6); full shifting key checklist + transparent XKB config (2.10–2.11) |
| 3 | Cross-platform | Win/Mac/Ride | Pending | TSF + IMK integration, Ride verification |
| 4 | Advanced | All | Pending | Keyword search, idioms, composition, overlay |

---

Design Notes: A Clean Scancode-First Architecture
==================================================

The implementation above grew incrementally: Ctrl and Alt were first handled in
`session.cc` using IBus keyvals, then AltGr and Super were added as evdev
intercepts in `mozc_engine.cc`, then a Ctrl evdev intercept was bolted on top
to fix UK/US layout conflicts. The result is two parallel code paths doing the
same job with different mechanisms.

This section describes what a clean design would look like if starting from
scratch — as a reference for any future refactoring.

The fundamental problem with keyvals
--------------------------------------

IBus delivers key events with two pieces of information:

- **`keyval`**: the Unicode codepoint (or keysym) the key produces on the
  *current layout*. UK Shift+2 gives `'"'`, US Shift+2 gives `'@'`. The same
  physical key produces different keyvals on different layouts.
- **`keycode`**: the Linux evdev scancode. Evdev 3 is always the key that says
  "2" on a US keyboard, regardless of what layout is active.

APL keyboard layouts are defined by *physical position* (a QWERTY-based
overlay), not by what character the key normally produces. Using keyvals for
APL glyph lookup therefore requires separate handling per keyboard layout —
which is exactly the UK/US bug that surfaced during testing.

The clean approach
------------------

All APL glyph output should be handled in `mozc_engine.cc` using evdev
scancodes, before `GetKeyEvent()` is called. `session.cc` should handle only
mode state (APL mode on/off, unshifted passthrough) and know nothing about
glyph lookup.

### Architecture

```
ProcessKeyEvent(keyval, keycode, modifiers)
  │
  ├── [Ctrl/Alt/Super bare key] → track held state; consume to suppress system effects
  │
  ├── [APL mode active AND shifting modifier held AND keycode in AplKeycodeToChars]
  │     chars = AplKeycodeToChars(keycode)        ← physical position, layout-independent
  │     is_shifted = (modifiers & IBUS_SHIFT_MASK)
  │     kc = is_shifted ? chars.shifted : chars.unshifted   ← US QWERTY normal or shifted char
  │     glyph = GetAplShiftedGlyph(kc) || GetAplGlyph(kc)  ← single lookup call
  │     if glyph: CommitText(glyph); return true
  │     else: return false   ← no APL mapping; pass to application, skip mozc server
  │
  ├── [APL mode active AND no shifting modifier AND keycode is printable]
  │     chars = AplKeycodeToChars(keycode)
  │     CommitText(is_shifted ? chars.shifted : chars.unshifted)   ← unshifted passthrough
  │     return true
  │
  └── [everything else] → GetKeyEvent() → session.cc → normal Mozc path
```

One intercept block in `mozc_engine.cc` serves *all* shifting keys uniformly.
The only per-modifier distinction needed is which physical modifier is held —
tracked using keyval for the bare key-down event (IBUS_Control_L vs
IBUS_Control_R, IBUS_Super_L vs IBUS_Super_R, etc.) and read from
`modifiers & IBUS_{MOD}_MASK` for combined Modifier+key events.

### `AplKeycodeToChars` as the single normalisation point

`AplKeycodeToChars` maps evdev scancode → `{unshifted_char, shifted_char}` in
US QWERTY. This is the *only* place that knows about physical key positions. All
of `apl_keymap.cc` — `GetAplGlyph` and `GetAplShiftedGlyph` — is then a pure
function from normalised US-QWERTY characters to APL glyphs. It knows nothing
about keyboards, scancodes, or layouts.

```
evdev scancode  →  AplKeycodeToChars  →  {unshifted, shifted} ASCII
                                              ↓
                                    GetAplGlyph / GetAplShiftedGlyph
                                              ↓
                                         APL glyph string
```

### What changes from the current implementation

| Current | Clean |
|---------|-------|
| Glyph lookup in `session.cc::TryAplShiftedKey()` using keyval | Glyph lookup in `mozc_engine.cc` using evdev scancode |
| Separate evdev intercepts for AltGr, Super; keyval path for Ctrl, Alt | Single intercept structure for all shifting keys |
| Unshifted passthrough in `session.cc::TryAplShiftedKey()` | Unshifted passthrough in `mozc_engine.cc` |
| `TryAplShiftedKey()` tries shifted map first (can't tell from keyval whether Shift was held) | `is_shifted = modifiers & IBUS_SHIFT_MASK` — explicit; no ambiguity |
| UK/US layout conflicts due to keyval encoding of Shift | No layout-dependent code; scancodes are layout-neutral |
| `session.cc` contains APL glyph logic; `mozc_engine.cc` also contains some | All APL glyph output in one place |

### What `session.cc` retains

In a clean design, `session.cc` retains only:

1. `apl_mode_active_` flag and the `CompositionModeAPL()` / `MakeSureIMEOn()`
   mode persistence logic.
2. `OutputMode()` override to report `commands::APL` to the property handler.
3. No glyph lookup. `TryAplShiftedKey()` is removed entirely.

### Cross-platform note

On platforms other than Linux/IBus, evdev scancodes are not available. Windows
uses virtual key codes; macOS uses hardware-independent key codes. A clean
cross-platform design would abstract `AplKeycodeToChars` behind a platform
interface and provide per-platform implementations:

- Linux/IBus: evdev scancodes (`keycode` parameter, evdev+8 convention)
- Windows: virtual key codes via `MapVirtualKey(vk, MAPVK_VK_TO_CHAR)` on a
  US QWERTY layout handle
- macOS: `kVK_*` constants from `<Carbon/Carbon.h>`

All three platforms have a stable physical-position-to-ASCII mapping for
QWERTY keyboards; the differences are only in how you get there.

---

Design Notes: Evdev vs XKB Symbols for Shifting Key Output
===========================================================

During development, an alternative approach to shifting-key glyph output was
considered — operating at the evdev/uinput level, as used by the keyboard
remapping tool Kanata (https://github.com/jtroo/kanata). This section records
the trade-off analysis and the rationale for choosing XKB symbols instead.

The evdev/uinput approach (Kanata-style)
-----------------------------------------

Kanata handles keyboard remapping on Linux by:

1. **Grabbing the input device exclusively** via `ioctl(EVIOCGRAB)` on the
   evdev device (`/dev/input/eventN`). This removes the device from the normal
   input chain — X11, Wayland, IBus, and all applications above them see
   nothing.

2. **Processing all events** in userspace: when a shifting modifier is held,
   inject the mapped output; otherwise re-inject the raw key event via a
   uinput virtual device so the rest of the system sees it normally.

3. **Writing output via uinput**: the virtual device appears to X11/Wayland as
   an ordinary keyboard. Modifier state (Ctrl, Alt, etc.) is synthesised as raw
   key events, which the compositor's own modifier-state machine then processes.

This approach cleanly solves the modifier-interception problem: because the
grab happens below X11/Wayland, compositor hotkeys and desktop-environment key
bindings never see the raw events. Kanata can therefore treat any modifier as
a shifting key without the conflicts that arise at the IBus level.

**Why it was not adopted for APL glyph output**

The fundamental limitation is that there is no reliable mechanism to inject an
arbitrary Unicode codepoint via uinput. The options available at that layer are:

| Method | Works in | Problems |
|--------|----------|----------|
| Ctrl+Shift+U sequence | GTK apps only | Fails in terminals, Qt, Electron, most Wayland-native apps |
| XDoTool-style keysym injection | X11 only | Broken on Wayland |
| Clipboard paste | Everywhere | Clobbers clipboard; introduces async timing issues |

Kanata exposes a `unicode` keyword for this purpose and documents that it has
problems in some applications — for exactly these reasons. Adopting the evdev
approach would trade the IBus modifier-interception problem for this Unicode
output problem, without actually resolving it.

Additional costs of the evdev approach:

- Requires membership of the `input` group (or root) — a security and
  packaging consideration not required for a pure IBus/XKB solution.
- Requires a separate always-running daemon (or tight integration into the
  `ibus-engine-mozc` process) with event-loop ownership of the device.
- Loop prevention: the daemon must distinguish its own injected events from
  physical events to avoid re-processing them.
- Cannot easily coexist with IBus for non-shifted input without careful
  passthrough routing.

Decision: XKB symbols as the shifting-key output mechanism
-----------------------------------------------------------

XKB (X Keyboard Extension) solves both the modifier-interception and Unicode
output problems cleanly:

- XKB runs inside the compositor or X server — below IBus, below applications.
  It processes key events before IBus sees them. A key combination bound at
  XKB Level 3 (AltGr) or any other XKB level is resolved to a Unicode
  codepoint by the compositor's own keyboard handling and delivered to the
  focused application as a normal character input. No Unicode injection is
  involved.
- Every application — terminal, Electron, Qt, GTK, native Wayland — receives
  the character through the standard text input path. There are no per-app
  compatibility issues.
- No daemon, no device grab, no root access required for user-space
  installation (see Packaging section below).

**Division of responsibility**

The XKB and IBus/Mozc layers are complementary and non-overlapping:

| Layer | Handles |
|-------|---------|
| XKB symbols file | Shifting-key glyph output (AltGr+key → APL glyph) |
| IBus / Mozc | Prefix input, keyword search, idiom lookup, prediction |

XKB intercepts shifted key combinations before IBus sees them. Mozc therefore
handles only unshifted keystrokes in its normal composition pipeline. The two
systems do not conflict.

This is the established approach used by existing APL input methods on Linux
(e.g. the `apl` XKB layout shipped with most distributions, and the layouts
distributed by Dyalog and GNU APL). Adopting it aligns with user expectations
and avoids reinventing a solved problem.

The implementation details of the XKB symbols file, option registration, and
per-desktop-environment activation are covered in Steps 2.10 and 2.11.

---

Design Notes: Packaging Considerations
=======================================

Distributing this tool through standard package repositories (apt, pacman,
rpm, etc.) is a goal. The Mozc binary and data files package straightforwardly,
but the XKB symbols file introduces complications that are worth recording.

Why XKB files are awkward to package
--------------------------------------

System-wide XKB files live in `/usr/share/X11/xkb/symbols/`. This directory
is owned by the `xkeyboard-config` package (Debian/Ubuntu: `xkb-data`; Fedora:
`xkeyboard-config`). To register a layout so it appears in desktop environment
keyboard settings, the rules file `evdev.xml` in the same package must also be
modified.

A package that writes into `/usr/share/X11/xkb/` either:

- **Conflicts with `xkb-data`**, which is present on virtually every Linux
  desktop installation, or
- **Uses a post-install script** to copy or merge files — which risks being
  silently overwritten when `xkb-data` is upgraded.

Neither is clean from a packaging perspective.

User-space installation path
------------------------------

libxkbcommon 1.0 (released 2020) added support for a user-level XKB search
path at `~/.config/xkb/`. Compositors and X servers using libxkbcommon honour
this path without requiring root access or touching system files:

```
~/.config/xkb/symbols/apl    ← the APL symbols file
~/.config/xkb/rules/          ← optional: local rules fragment
```

This is the preferred installation path. It is supported by most modern
Wayland compositors (GNOME/Mutter, KDE/KWin, sway, Hyprland) and X11 setups
using libxkbcommon. The limitation is that user-installed layouts do not appear
in desktop environment keyboard settings GUIs — activation must be done via
`setxkbmap`, `gsettings`, or equivalent (see Step 2.11 for per-DE details).

Realistic packaging outcome
-----------------------------

A two-tier approach is expected:

| Component | Packaging |
|-----------|-----------|
| Mozc APL binary + data files | Standard distro package (deb/rpm/AUR PKGBUILD) |
| XKB symbols file | Installed to `~/.config/xkb/` by a post-install hook or `mozc-apl-setup` helper command |
| XKB activation | Per-DE, handled by `mozc-apl-setup` (detects session/desktop and applies the appropriate mechanism — see Step 2.11) or documented as manual steps |

The `mozc-apl-setup` helper approach is common in the IME ecosystem: `im-config`
and `im-chooser` already perform desktop-environment detection to activate input
methods. A small setup command that sniffs `XDG_SESSION_TYPE` and
`XDG_CURRENT_DESKTOP` and applies the correct `setxkbmap`/`gsettings`/
`kwriteconfig` incantation is packageable, auditable, and reversible.

For package repositories that do not support post-install scripts well (e.g.
some Flatpak and Snap configurations), or for desktops that are not
automatically detected, the fallback is a documented set of manual steps per
operating system and desktop environment. This is not unusual: the upstream
Mozc packages on most distributions already require manual IME activation steps
after installation. The XKB step is one additional operation of the same
character.

**Tentative packaging plan per distribution type**

| Distribution | Binary package | XKB setup |
|---|---|---|
| Debian/Ubuntu (deb) | Package with `Recommends: mozc-apl-setup` | Post-install script installs to `~/.config/xkb/` and runs DE detection |
| Arch Linux (AUR) | PKGBUILD with `install` hook | Same |
| Fedora (rpm) | spec with `%post` | Same |
| Flatpak/Snap | Bundled binary | Manual instructions only (sandbox restrictions prevent XKB file installation) |
| Source build | `install.sh` already present | Extend `install.sh` with XKB step |
