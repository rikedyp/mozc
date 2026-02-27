APL Glyph Input — Linux / IBus Implementation
==============================================

This document covers the Linux/IBus-specific implementation of APL shifting-key
input. For the project overview, architecture, and cross-platform plans see
[apl_overview.md](apl_overview.md).

---

Candidate Shifting Keys (Linux)
--------------------------------

| Modifier | Status | Notes |
|----------|--------|-------|
| Ctrl | **Implemented** | Standard APL keyboard convention. Works on Wayland; XWayland/Electron limitation documented in `ctrl_key_wip.md`. |
| Left Alt | **Implemented** | Works via IBus. Bare Alt consumed to suppress menu-bar focus activation. |
| Right Alt (AltGr) | Deferred → XKB | IBus cannot reliably intercept MOD5 before XKB resolves AltGr combos. Will be implemented via XKB symbols file. |
| Caps Lock | Deferred → XKB or IBus CAPS flag | Requires further investigation. |
| Super (Windows key) | Not supported | Desktop environment grabs most Super+key combos before IBus sees them. |

---

Key Files (Linux)
-----------------

| File | Role |
|------|------|
| `src/unix/ibus/mozc_engine.cc` | IBus frontend: key event interception, shifting key detection, bare-modifier tracking, AltGr/evdev intercept |
| `src/unix/ibus/mozc_engine.h` | Engine members: held-state bools, cached `AplShiftingKeySet` |
| `src/unix/ibus/property_handler.cc` | IBus panel menu: APL mode radio buttons, shifting key submenu |
| `src/unix/ibus/property_handler.h` | Property handler declarations |
| `src/unix/ibus/ibus_config.proto` | IBus engine config proto (includes APL composition mode) |
| `src/session/session.cc` | `TryAplShiftedKey()`, `CompositionModeAPL()`, `apl_mode_active_` |
| `src/session/session.h` | `apl_mode_active_`, `TryAplShiftedKey()` declaration |
| `src/session/apl_keymap.cc` | APL glyph table: `GetAplGlyph()`, `GetAplShiftedGlyph()` |
| `src/session/apl_keymap.h` | Glyph table declarations |
| `src/protocol/config.proto` | `AplShiftingKeySet` message, deprecated `AplShiftingKey` enum |
| `src/protocol/commands.proto` | `CompositionMode::APL = 6` |

---

Phase 1: Minimal Linux POC — Verified ✓
========================================

**Goal**: Prove the mechanism works end-to-end. When the IME is in APL mode and
the user presses Ctrl+A, ⍺ is committed to the application.

**Approach**: Intercept key events at the session level, before the normal keymap
lookup. When APL mode is active and Ctrl is held, look up the base key in the
APL glyph table and commit the result directly, bypassing the
keymap/composer/converter pipeline.

### What was implemented

**`src/protocol/commands.proto`**: Added `APL = 6` to `CompositionMode`.

**`src/session/apl_keymap.h/.cc`**: New header-only lookup table mapping ASCII
key codes to APL glyphs (Dyalog US keyboard layout as the base).

**`src/session/session.h/.cc`**: Added `apl_mode_active_` flag and
`TryAplShiftedKey()`. Called at the top of `SendKeyPrecompositionState()`,
`SendKeyCompositionState()`, and `SendKeyConversionState()`. When APL mode is
active and the configured shifting key is held, looks up the base key and
commits the glyph directly.

**`src/session/session.cc` — `CompositionModeAPL()`**: Sets Composer to
`HALF_ASCII` (passthrough transliteration), sets `apl_mode_active_ = true`,
overrides output mode to `commands::APL`.

**`src/unix/ibus/property_handler.cc`**: Added APL entry to
`kMozcEngineProperties` (symbol "⍺", display label "APL"), fixing the "_A"
taskbar icon regression from Phase 1.

**Result**: Verified ✓ on Linux/Wayland/KDE (2026-02-15). Ctrl+key produces APL
glyphs in Kate and VSCode (Wayland session). See `ctrl_key_wip.md` for the
XWayland/Electron limitation.

**Note on testing after rebuild**: `install.sh` restarts `ibus-engine-mozc` but
not `mozc_server`. Session-level changes run inside `mozc_server`; after
installing, run `killall mozc_server` — it relaunches automatically on the next
keystroke.

---

Phase 2: Polish and Configuration — Verified ✓
================================================

### Step 2.0: Fix taskbar icon — Verified ✓

Subsumed by Step 2.1c. Adding APL to `kMozcEngineProperties` gave the property
handler the correct symbol ("⍺") for APL mode. The "_A" display was caused by
the property handler falling through to the HALF_ASCII entry.

### Step 2.1: APL mode persistence — Verified ✓

**Problem**: APL mode did not survive Enable/FocusIn cycles, autocomplete
commits, or focus changes. `apl_mode_active_` was cleared whenever
`TURN_ON_IME` or `SWITCH_COMPOSITION_MODE` arrived with a non-APL mode, because
the IBus frontend had no awareness of APL as a composition mode.

**Fix**: Made APL a first-class composition mode throughout the IBus frontend.

- **`src/unix/ibus/ibus_config.proto`**: Added `APL = 6` to `Engine.CompositionMode`.
- **`src/unix/ibus/mozc_engine.cc`**: Added APL case to `ConvertCompositionMode()`.
- **`src/unix/ibus/property_handler.cc`**: Added APL entry to `kMozcEngineProperties`.
- **`src/session/session.cc`**: Handled APL in `MakeSureIMEOn()` / `TURN_ON_IME`
  path; audited `apl_mode_active_ = false` sites.

**Result**: Mode survives application switches, focus changes, autocomplete,
Enter key, and Win+Space IME switching. Taskbar shows "⍺" consistently.

### Step 2.2: Suppress autocomplete in APL mode — Verified ✓

Extended `TryAplShiftedKey()` to handle unshifted printable ASCII (0x20–0x7e)
in addition to shifted combinations. Unshifted keys are committed directly,
bypassing Composer/Suggest/Converter, so no autocomplete popup appears in APL
mode. Special keys (Backspace, Enter, arrows) use `special_key` not `key_code`
and fall through to the normal keymap handler unchanged.

### Step 2.3: Ctrl+Shift for shifted APL glyphs

Added `GetAplShiftedGlyph()` alongside `GetAplGlyph()` in `apl_keymap.cc`.
When the shifting modifier is held, `TryAplShiftedKey()` checks the shifted
table first, then falls back to the unshifted table.

### Step 2.4: Configurable shifting key — Verified ✓ (Ctrl and Left Alt)

Added `AplShiftingKey` enum to `protocol/config.proto` (deprecated), then
`AplShiftingKeySet` message with per-key bools. Exposed in the IBus panel as a
submenu (Step 2.10). `TryAplShiftedKey()` reads `context_->GetConfig()` for the
server-side config.

**AltGr is not handled via IBus.** AltGr (Right Alt) events are resolved by
XKB before IBus sees them. The IBus path for AltGr was explored but is not
retained — see Design Notes: Evdev vs XKB Symbols below.

### Step 2.4a: Fix Ctrl+key shortcuts broken when Alt is the shifting key — Verified ✓

The unshifted-passthrough path was committing bare characters even when a
non-shifting modifier (e.g., Ctrl) was held. Added a guard: if any modifier
other than Shift/Left Shift/Right Shift/Caps is present and it is not the
configured shifting modifier, return `false` (pass through to application).

**Files**: `src/session/session.cc`

### Step 2.4b: Fix menu-bar focus activation when Alt is the shifting key — Verified ✓

When the IME consumes Alt+A (APL glyph committed), the application saw
Alt-down then Alt-up with no non-modifier key in between, interpreted as
"Alt tapped" → menu-bar focus.

**Fix**: In `MozcEngine::ProcessKeyEvent()`, consume bare Alt_L/Alt_R/Meta_L/Meta_R
events when Alt is the configured APL shifting key and APL mode is active.
Track held state in `left_alt_held_`; inject `LEFT_ALT` into the `KeyEvent`
after `GetKeyEvent()` returns (since the bare Alt event is consumed before
`GetKeyEvent()` runs, so `currently_pressed_modifiers_` never records it).

**Files**: `src/unix/ibus/mozc_engine.cc`, `src/unix/ibus/mozc_engine.h`

### Step 2.5: Remove Japanese modes for early-access release

For the early-access POC, `kMozcEngineProperties` retains only APL and Direct
entries. Japanese modes remain in the binary but are not exposed. The default
composition mode is APL.

**Files**: `src/unix/ibus/property_handler.cc`, `src/unix/ibus/mozc_engine.cc`

### Step 2.6: Verify glyph table correctness — Pending

Review `src/session/apl_keymap.cc` against the Dyalog US keyboard layout.
**Note**: The current implementation uses different code paths for different
shifting keys (Ctrl uses evdev scancodes in `mozc_engine.cc`; Alt uses keyvals
in `session.cc`). This means the same physical key can produce different results
depending on the shifting key and keyboard layout (e.g., Alt+Shift+' on a UK
layout produces the wrong glyph). This inconsistency will be resolved as part
of the planned code cleanup (see "Planned Next Steps" below) before a detailed
glyph table audit is worthwhile.

### Step 2.9: Visual feedback — Verified ✓

IBus language bar shows "⍺" when APL mode is active (implemented as part of
Step 2.1c).

### Step 2.10: Multi-key shifting key submenu — Verified ✓ (Ctrl and Left Alt)

Replaced the two-option dropdown in the Qt config dialog with a submenu of
toggle properties in the IBus panel. Each entry is a `PROP_TYPE_TOGGLE`
property.

**Protocol**: `AplShiftingKeySet` message in `config.proto` with per-key bool
fields (`left_ctrl`, `right_ctrl`, `left_alt`, `right_alt`, `caps_lock`,
`left_super`, `right_super`). See "Planned Next Steps" for the intended
simplification.

**IBus panel**: `kAplShiftingKeyProps[]` in `property_handler.cc` lists all
options; `AppendAplShiftingKeyPropertyToPanel()` builds the submenu;
`ProcessPropertyActivate()` toggles and persists the selection.

**Known bug**: Left Ctrl and Right Ctrl are not independently functional. The
Ctrl intercept in `mozc_engine.cc` checks `IBUS_CONTROL_MASK` (combined) rather
than per-side held state, so both Ctrl keys activate APL glyphs regardless of
the menu selection. Fix is documented in "Planned Next Steps".

**Menu state sync bug (fixed)**: `ProcessPropertyActivate` previously updated
`Config` via `SetConfig` but never called `prop.SetState()` +
`engine->UpdateProperty()` on the toggled item, so checkmarks reset on every
`FocusIn`. Fixed by adding `prop.SetState(new_value ? PROP_STATE_CHECKED :
PROP_STATE_UNCHECKED)` + `engine->UpdateProperty(&prop)` after `SetConfig`.

---

Planned Next Steps (Linux)
===========================

### Clean up the shifting key implementation

The current implementation has grown to support more shifting keys than are
viable via IBus alone. The plan is to strip it back to what works cleanly:

**Supported in IBus (keep)**:
- Left Ctrl (independent of Right Ctrl)
- Right Ctrl (independent of Left Ctrl)
- Left Alt

**Handled via XKB symbols (not in IBus)**:
- Right Alt (AltGr) — XKB resolves this before IBus sees it; ship an XKB
  symbols file instead (see Design Notes: Evdev vs XKB Symbols)
- Caps Lock — IBus CAPS flag approach is an option, but defer
- Super — desktop environment grabs too many combos

**Code changes required**:

1. **`src/protocol/config.proto`**: Remove `right_alt`, `caps_lock`,
   `left_super`, `right_super` from `AplShiftingKeySet`. Reserve field numbers
   4–7. Keep deprecated `AplShiftingKey` enum (backward compat).

2. **`src/unix/ibus/property_handler.cc`**: Trim `kAplShiftingKeyProps[]` to
   three entries (Left Ctrl, Right Ctrl, Left Alt). Remove extra cases from
   `GetAplShiftKeyValue()` / `SetAplShiftKeyValue()`.

3. **`src/unix/ibus/mozc_engine.h`**: Remove `caps_lock_held_`,
   `right_alt_held_`, `left_super_held_`, `right_super_held_`. Add
   `left_ctrl_held_`, `right_ctrl_held_`.

4. **`src/unix/ibus/mozc_engine.cc`**:
   - Remove Caps Lock intercept block
   - Remove AltGr/MOD5 intercept block
   - Remove Super tracking and intercept blocks
   - Remove `right_alt_held_` injection
   - Add L/R Ctrl tracking (observe bare `IBUS_Control_L` / `IBUS_Control_R`
     events, set held state, do NOT consume — no menu-bar focus issue with Ctrl)
   - Update Ctrl intercept condition from `IBUS_CONTROL_MASK` to
     per-side held-state check (strict, no fallback)
   - Update `FocusOut()` to reset new vars, remove old ones
   - Update `UpdatePreeditMethod()` fallback: `APL_SHIFT_ALT` → `left_alt` only

5. **`src/session/session.cc`**: Remove `right_alt`, `left_super`, `right_super`
   branches from `TryAplShiftedKey()`.

### Verify glyph table

After the above cleanup (single code path for all supported shifting keys),
audit `src/session/apl_keymap.cc` against Dyalog US layout.

### XKB symbols file for AltGr

Implement `data/xkb/apl` with an `altgr` variant (see Design Notes: XKB Symbols
below). Integrate with `install.sh` and the Qt config dialog.

---

Design Notes: Scancode-First Architecture
==========================================

The implementation grew incrementally: Ctrl and Alt were first handled in
`session.cc` using IBus keyvals, then Ctrl was moved to `mozc_engine.cc` using
evdev scancodes to fix UK/US layout conflicts.

**Why scancodes?** IBus delivers `keyval` (the Unicode codepoint the key
produces on the *current layout*) and `keycode` (the Linux evdev scancode,
physical-position-based, layout-independent). APL keyboard layouts are defined
by physical position (a QWERTY-based overlay), so keyval is layout-dependent
and gives wrong results on non-US layouts. Evdev scancode 30 is always the
QWERTY 'A' key, regardless of what the active layout labels it.

**Current state**: Ctrl glyph output uses scancodes (correct). Alt glyph output
still uses keyvals in `session.cc` (layout-dependent — the UK/US glyph
inconsistency). This inconsistency will persist until the Alt path is also moved
to `mozc_engine.cc`.

**Clean architecture (future)**:

All APL glyph output should be handled in `mozc_engine.cc` using evdev
scancodes, before `GetKeyEvent()` is called. `session.cc` should retain only:
1. `apl_mode_active_` flag and mode persistence logic
2. `OutputMode()` override to report `commands::APL` to the property handler
3. No glyph lookup — `TryAplShiftedKey()` would be removed

```
ProcessKeyEvent(keyval, keycode, modifiers)
  │
  ├── [Ctrl/Alt bare key] → track held state; consume if needed (Alt only)
  │
  ├── [APL mode active AND shifting modifier held AND keycode in AplKeycodeToChars]
  │     chars = AplKeycodeToChars(keycode)        ← physical position, layout-independent
  │     is_shifted = (modifiers & IBUS_SHIFT_MASK)
  │     kc = is_shifted ? chars.shifted : chars.unshifted
  │     glyph = GetAplShiftedGlyph(kc) || GetAplGlyph(kc)
  │     if glyph: CommitText(glyph); return true
  │     else: return false
  │
  ├── [APL mode active AND no shifting modifier AND keycode is printable]
  │     CommitText(unshifted/shifted char)   ← autocomplete suppression
  │     return true
  │
  └── [everything else] → GetKeyEvent() → session.cc → normal Mozc path
```

One intercept block in `mozc_engine.cc` serves all shifting keys uniformly.
`AplKeycodeToChars()` is the single normalisation point.

---

Design Notes: Evdev vs XKB Symbols for Shifting Key Output
===========================================================

### Why not use uinput for glyph output directly

An approach of using evdev/uinput to inject APL glyphs as output was
considered and rejected. There is no reliable mechanism to inject arbitrary
Unicode codepoints via uinput:

| Method | Works in | Problems |
|--------|----------|---------|
| Ctrl+Shift+U sequence | GTK apps only | Fails in terminals, Qt, Electron, most Wayland-native apps |
| XDoTool keysym injection | X11 only | Broken on Wayland |
| Clipboard paste | Everywhere | Clobbers clipboard; async timing issues |

Note: uinput *can* reliably output Linux `KEY_*` keycodes — the limitation
above is specific to injecting Unicode codepoints as output.

**Decision: XKB symbols as the output mechanism for AltGr and other
hard-to-intercept keys.**

XKB runs inside the compositor or X server — below IBus, below applications.
It resolves key combinations to Unicode codepoints and delivers them as normal
character input. Every application receives the character through the standard
text input path. No daemon, no device grab, no root access required for
user-space installation (`~/.config/xkb/`).

**Division of responsibility**:

| Layer | Handles |
|-------|---------|
| XKB symbols file | Shifting-key glyph output (AltGr+key → APL glyph) |
| IBus / Mozc | APL mode state, Ctrl/Alt interception, keyword search (future), idiom lookup (future) |

This is the established approach used by existing APL input methods on Linux
(e.g. the `apl` XKB layout shipped with most distributions, and the layouts
distributed by Dyalog and GNU APL).

### Investigation: Kanata for arbitrary shifting keys

**Conclusion: not viable. XKB remains the correct approach.**

The XKB+IBus design cannot support arbitrary user-chosen shifting keys because
some combinations (e.g. `Super+letter`) are grabbed by the compositor before
IBus sees them. This was investigated as a potential limitation worth solving:
every user has different applications, locales, and desktop configurations that
restrict which key combinations are practical.

Kanata — a keyboard remapper operating at the evdev level — was investigated
as a potential solution. Kanata grabs the physical input device directly, so
the compositor never sees the original keypress. This was confirmed to work on
KDE Plasma 6: `Super+D` (normally minimises all windows) was intercepted by
Kanata before KDE acted on it.

**However, the signalling problem is fatal to this approach.**

For Kanata to communicate which APL glyph to output, it must emit a signal
that IBus can interpret. Two options were considered:

*Raw keycodes*: Kanata emits an unused Linux `KEY_*` code; IBus maps it to a
glyph. Tested with `KEY_F13` — KDE Plasma opened a menu, confirming that the
compositor grabs the virtual device's output before IBus sees it. There is no
guaranteed "safe" keycode: compositors can bind any named keysym, and
different DEs have different defaults.

*Modifier normalisation*: Kanata emits `[letter]` with a rarely-used modifier
bit set — e.g. `Hyper` or `ISO_Level5_Shift` — and IBus checks for that
modifier. However, on Wayland, modifier state is computed entirely by XKB from
the active keymap. For either modifier to appear in the event, the keymap must
define a key as generating that modifier. This requires an XKB symbols file
entry. At that point the XKB file is doing the essential work, and Kanata is
only adding the "remap arbitrary physical key → that modifier" step — with
significant added cost:

- Kanata requires a separate install and a daemon running before the session
- Requires `input` group membership or a udev rule (elevated device access)
- Any process with `/dev/input/event*` access can read all keystrokes,
  including passwords — a trust concern for many users
- The Kanata config must stay in sync with the IBus shifting-key setting

Since an XKB symbols file is needed regardless, the full shifting-key layout
can be implemented in XKB alone with no additional tooling. This is exactly
how the established APL input methods (Dyalog, GNU APL, the `apl` XKB layout)
handle it. **The limitation stands**: shifting keys are restricted to
combinations that compositors do not grab, which in practice means AltGr and
(on most DEs) Ctrl and Alt for letter keys.

**XKB symbols file structure** (`data/xkb/apl`):

```
partial alphanumeric_keys
xkb_symbols "altgr" {
    include "level3(ralt_switch)"
    key <AD01> { [ q, Q, 0x1000FD3 ] };  // ⍳
    key <AD02> { [ w, W, 0x1000279 ] };  // ⍹
    // ... full layout
};

partial alphanumeric_keys modifier_keys
xkb_symbols "caps" {
    include "level3(caps_switch)"
    key <AD01> { [ q, Q, 0x1000FD3 ] };
    // ...
};
```

Activated via `setxkbmap -option apl:altgr` or through GNOME/KDE keyboard
settings. Option strings must be registered in a rules fragment.

**Installation**: user-space path `~/.config/xkb/` (libxkbcommon ≥ 1.0,
supported by most modern Wayland compositors and X11). Avoids conflicting with
the `xkb-data` system package.

---

Design Notes: Packaging Considerations
=======================================

**Mozc binary + data files**: standard distro package (deb/rpm/AUR PKGBUILD).

**XKB symbols file**: installed to `~/.config/xkb/` by a post-install hook or
`mozc-apl-setup` helper command. The system path (`/usr/share/X11/xkb/symbols/`)
conflicts with the `xkb-data` package and risks being overwritten on upgrade.

**XKB activation**: per-DE, handled by `mozc-apl-setup` (detects session/desktop
and applies the appropriate `setxkbmap`/`gsettings`/`kwriteconfig` command) or
documented as manual steps.

| Distribution | Binary package | XKB setup |
|---|---|---|
| Debian/Ubuntu (deb) | Package with `Recommends: mozc-apl-setup` | Post-install script |
| Arch Linux (AUR) | PKGBUILD with `install` hook | Same |
| Fedora (rpm) | spec with `%post` | Same |
| Flatpak/Snap | Bundled binary | Manual instructions only (sandbox restrictions) |
| Source build | `install.sh` present | Extend `install.sh` with XKB step |

---

Phase 3 (Linux-relevant): Ride / XWayland Limitation
======================================================

Ride (Dyalog APL IDE) uses an older Electron version that runs under XWayland
even in a Wayland session. On XWayland, Chromium/Electron's accelerator table
fires before the IBus D-Bus response, so `consumed=true` cannot suppress
Ctrl+key shortcuts. Ctrl+A in Ride triggers "select all" rather than ⍺.

**Decision**: Accept this limitation. Ride is expected to update its Electron
version before this IME ships. Right Alt (AltGr via XKB) is a viable workaround
since it is not in Electron's accelerator table.

See [ctrl_key_wip.md](ctrl_key_wip.md) for the full root cause analysis and
evidence.
