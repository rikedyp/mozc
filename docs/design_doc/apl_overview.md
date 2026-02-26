APL Glyph Input — Overview and Architecture
============================================

Objective
---------

Enable APL glyph input by designating one or more modifier keys as an "APL
shifting key". While the shifting key is held, ordinary key presses produce APL
glyphs instead of their Latin equivalents — mirroring the physical APL keyboard
overlays used historically and in modern APL environments.

Background
----------

Traditional APL keyboards assign APL glyphs to the shifted or "APL-shifted"
positions of standard keys. On a modern keyboard without a physical APL overlay,
this is typically emulated by using a modifier key as a mode-shift: holding the
modifier causes each key to emit the APL glyph assigned to that key position.

Different APL implementations use different shifting keys. We want to support
several candidates, chosen per platform, to find what works best while avoiding
conflicts with existing system or application shortcuts.

Goals
-----

1. Implement shifting-key APL glyph input on Linux (IBus), macOS (IMK), and
   Windows (TSF), using the most appropriate mechanism per platform.
2. Ensure the shifting key is configurable per user.
3. Support keyword search, idiom lookup, and glyph composition as stretch goals.

---

Architecture Overview
=====================

How Key Events Flow Through Mozc
---------------------------------

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

### Composition Modes

Mozc defines modes in `protocol/commands.proto`:

```protobuf
enum CompositionMode {
  DIRECT = 0;  HIRAGANA = 1;  FULL_KATAKANA = 2;
  HALF_ASCII = 3;  FULL_ASCII = 4;  HALF_KATAKANA = 5;
  APL = 6;   // added for APL mode
}
```

APL is a first-class composition mode, enabling mode switching via the existing
`SessionCommand::SWITCH_COMPOSITION_MODE` mechanism.

### Architecture Note: HALF_ASCII as the Underlying Transliteration Mode

The APL shifting-key mode sets the Composer's transliteration mode to
`HALF_ASCII` rather than introducing a new `TransliterationType`. A session-level
`apl_mode_active_` flag tracks APL state independently, and `OutputMode()`
overrides the reported `CompositionMode` to `APL` so the property handler and
platform panel display the correct icon/label.

### Cross-Platform Structure

APL glyph lookup (`GetAplGlyph`, `GetAplShiftedGlyph`) and mode state
(`apl_mode_active_`) live in the platform-independent session layer.
Platform frontends handle key event interception and mode-switch UI.

```
            ┌─────────────────┐
            │  Session Layer  │  ← TryAplShiftedKey() / apl_mode_active_
            │  (apl_keymap)   │    shared across platforms
            └───────┬─────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
   ┌─────────┐ ┌─────────┐ ┌─────────┐
   │  IBus   │ │   TSF   │ │   IMK   │
   │ (Linux) │ │ (Win32) │ │ (macOS) │
   └─────────┘ └─────────┘ └─────────┘
   Submenu in   Language     Input menu
   IBus panel   bar button   in menu bar
```

---

Current Status
==============

| Phase | Scope | Platform | Status |
|-------|-------|----------|--------|
| 1 | Minimal POC: Ctrl+key → APL glyph | Linux | **Done** (2026-02-15) |
| 2 | Polish: mode persistence, shifting key config, suppressed autocomplete | Linux | **Done** (core); cleanup planned — see `apl_linux.md` |
| 3 | Cross-platform: macOS, Windows | macOS, Windows | Pending — see below |
| 4 | Advanced: keyword search, idiom lookup, composition | All | Pending |

---

Phase 3: Cross-Platform Support (Planned)
==========================================

### Shared Session Layer

`TryAplShiftedKey()` lives in the platform-independent session layer and works
identically on all platforms. Only the UI integration (menus, language bar,
icons) and key event interception are platform-specific.

### Step 3.1: macOS (InputMethodKit)

**Key integration points:**

- `src/mac/mozc_imk_input_controller.mm`: The main InputMethodKit controller.
  Add APL mode handling. The `-(NSMenu *)menu` method returns a static NSMenu
  from a XIB; APL mode toggle and shifting key selection must be added here or
  built programmatically.

- `src/mac/English.lproj/Config.xib`: The static IBOutlet menu. Currently has
  Reconversion, Preferences, Add a word, Dictionary Tool, About. APL mode items
  need to be added.

- `src/mac/KeyCodeMap.mm`: Maps macOS virtual key codes to Mozc keycodes. No
  changes needed for basic Ctrl+key — the mapping already handles modifier keys.

- **macOS-specific shifting key options:**
  - Control: works, equivalent to Linux Ctrl
  - Option (Left Alt equivalent): needs testing; macOS uses Option for special
    character input on some layouts, which may conflict
  - Command: generally grabbed by the system, likely not viable
  - Caps Lock: different handling on macOS; needs investigation

- **macOS menu**: Unlike IBus (programmatic IBusProp tree), the macOS menu is a
  static NSMenu XIB wired via IBOutlet. Adding dynamic mode items will require
  either editing the XIB or building menu items programmatically in
  `activateServer:` / `handleConfig`.

- **Shifting key config**: `AplShiftingKeySet` proto fields are Linux-centric
  (left_alt, left_super, etc.). macOS will need its own representation — either
  new proto fields or a separate message — once macOS-specific options are
  known.

See `apl_macos.md` (to be created when macOS work begins) for implementation
details.

### Step 3.2: Windows (TSF/IMM32)

- `src/win32/base/conversion_mode_util.cc`: Add mapping for `commands::APL`.
- `src/win32/tip/`: TSF language bar compartment — add APL mode entry.
- `src/win32/ime/`: IMM32 legacy interface — add APL support.
- The session-level `TryAplShiftedKey()` works unchanged; only UI integration
  differs.

---

Phase 4: Advanced Features (Stretch Goals)
==========================================

### Step 4.1: Keyword search

Allow the user to type a keyword (e.g., "reverse") and see matching APL glyphs
as candidates. Implementation path: add APL symbol entries to the Mozc
dictionary with keyword readings, or create a dedicated APL rewriter
(`src/rewriter/apl_symbol_rewriter.cc`).

### Step 4.2: Idiom search (APLCart-style)

Integrate an APL idiom database. When the user types a description like
"remove duplicates", show APL idioms as candidates.

### Step 4.3: Glyph composition / overstrike

Support composing glyphs by combining base characters:
- Tab completion mode: type partial glyph name, press Tab to complete
- Overstrike mode: ○ combined with | → ⌽ (rotate)

### Step 4.4: On-screen keyboard overlay

Show a floating window displaying the APL keyboard layout, updating in
real-time as modifier keys are pressed/released (a renderer feature,
`src/renderer/`).

---

Related Documents
=================

- [apl_linux.md](apl_linux.md) — Linux/IBus implementation record, design
  notes, and planned next steps
- [ctrl_key_wip.md](ctrl_key_wip.md) — XWayland limitation analysis (Ctrl+key
  not interceptable in XWayland/X11 Electron apps)
