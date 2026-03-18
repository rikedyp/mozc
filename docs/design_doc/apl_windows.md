APL Shifting Key — Windows / TSF Proof of Concept
===================================================

This document covers the Windows-specific proof of concept for APL shifting-key
input using Microsoft's Text Services Framework (TSF). For the macOS
implementation see [apl_macos.md](apl_macos.md).

---

Scope
-----

This POC validates four things:

1. **Installation**: Mozc registers as an **English** input method on Windows
   via TSF, with no Japanese profile.
2. **System tray menu**: The language bar exposes an "APL Shifting Key" submenu
   with checkable items for each modifier key.
3. **Config persistence**: The user's shifting key selection round-trips through
   `config.proto` via the Mozc server and survives IME restarts.
4. **APL glyph production**: Holding a configured shifting key and pressing a
   character key inserts the corresponding APL glyph, consuming the keystroke
   before the application sees it.

**Out of scope for this POC**: APL mode toggling (switching between APL and
normal input), cross-app testing beyond Notepad/VSCode, and non-US keyboard
layout support. Those will follow in a subsequent phase.

---

Overview
--------

Windows uses Microsoft's **Text Services Framework (TSF)** rather than IMK or
IBus. The Mozc TIP (Text Input Processor) is implemented as a COM DLL
(`mozc_tip64.dll`) that registers with the TSF framework. Key events arrive via
`ITfKeyEventSink::OnTestKeyDown` / `OnKeyDown` — these are **synchronous**,
like macOS IMK and unlike Linux IBus. The TIP returns `eaten=TRUE` to consume a
key before the application sees it.

The language bar (system tray input indicator) is managed via `ITfLangBarItemMgr`
and provides menus through `ITfLangBarItemButton::InitMenu` / `OnMenuSelect`.
This is the Windows equivalent of the macOS NSMenu and the Linux IBus panel.

---

Available Modifiers (Windows)
------------------------------

Windows provides left/right modifier distinction via `GetKeyState()` on
specific virtual key codes. This is a significant advantage over macOS.

| Modifier | VK Code | Pros | Cons |
|----------|---------|------|------|
| Left Ctrl | `VK_LCONTROL` | Familiar | Conflicts with Ctrl+C/V/X/Z/A/S in every app |
| Right Ctrl | `VK_RCONTROL` | Far fewer shortcut conflicts | Missing on some laptop keyboards |
| Left Alt | `VK_LMENU` | Familiar | Bare press activates menu bar in Win32 apps |
| Right Alt | `VK_RMENU` | Clean on US keyboards | Is AltGr on international layouts — would break accented character input |
| Caps Lock | `VK_CAPITAL` | Ergonomic home-row position; rarely used for its default purpose | Toggle key — TIP must eat the keydown to prevent Caps Lock state from toggling |

All five are supported in this POC with multiple simultaneous selection (e.g.,
the user can enable both Right Ctrl and Caps Lock at the same time).

### Caps Lock handling on Windows

TSF delivers `VK_CAPITAL` key events to `OnTestKeyDown` / `OnKeyDown` *before*
the Caps Lock toggle takes effect. If the TIP returns `eaten=TRUE`, the toggle
is suppressed entirely. This is cleaner than macOS, where Caps Lock is stripped
in `KeyCodeMap.mm` and requires interception before that stripping.

For the POC, Caps Lock appears as a menu item only. The actual key interception
(eating `VK_CAPITAL` to prevent toggle) will be implemented in the glyph
insertion phase.

### Left Alt bare-press side effect

On Windows, releasing Alt without pressing another key activates the menu bar
in Win32 applications. The Linux implementation handles this by suppressing
bare-Alt release in the IME frontend. The Windows TIP can do the same in
`OnKeyUp` by eating `VK_LMENU` releases when APL mode is active and Left Alt
is the shifting key.

---

Registration Architecture
--------------------------

### Current state

Mozc registers as a **Japanese** input method:
- Text Service GUID: `{10A67BC8-22FA-4A59-90DC-2546652C56BF}`
- Profile GUID: `{186F700C-71CF-43FE-A00E-AACB1D9E6D3D}`
- Language ID: `0x0411` (`MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT)`)
- Display name: `"Mozc"` (from `IDS_TEXTSERVICE_DISPLAYNAME_SYNONYM` in `tip_resource.rc`)
- Registration: `ITfInputProcessorProfiles::AddLanguageProfile()`
- Enablement: `InstallLayoutOrTip(L"0x0411:{CLSID}{ProfileGUID}", 0)`

### POC approach: APL-only (replace Japanese with English)

For the POC, we **replace** the Japanese profile with an English one rather
than adding a second profile alongside it. This avoids dual-profile detection
logic, conditional language bar initialization, and the complexity of two
profiles sharing one DLL.

Changes to existing constants:
- `tsf_profile.cc`: `kTextServiceLanguage` → `MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT)` (`0x0409`)
- `tip_resource.rc`: display name string → `"Mozc APL"`
- `custom_action.cc`: `EnableTipProfile` → `0x0409:{CLSID}{ProfileGUID}`

Everything else (text service GUID, profile GUID, COM registration, category
registration) stays the same. The TIP DLL is unchanged architecturally — it
just registers under English instead of Japanese.

This means:
- Windows Settings → Time & Language → Language → English → Keyboard shows
  "Mozc APL"
- The system tray input indicator shows "Mozc APL" when selected
- The language bar always shows the APL shifting key menu (no Japanese input
  mode toggle)
- Japanese Mozc functionality is not available on this branch (by design)

### Key files to modify

| File | Change |
|------|--------|
| `src/win32/base/tsf_profile.cc` | Change `kTextServiceLanguage` to English |
| `src/win32/tip/tip_resource.rc` | Change display name to "Mozc APL" |
| `src/win32/custom_action/custom_action.cc` | Change `0x0411` to `0x0409` in `EnableTipProfile` |

---

Language Bar Menu Design
-------------------------

### Current language bar structure

```
[Input Mode Toggle]  →  Direct | Hiragana | Full Katakana | ...
[Tool Menu]          →  Dictionary | Word Register | Properties | Cancel
[Help Menu]          →  About | Help
```

### APL language bar structure (replacing Japanese menus)

Since this is APL-only, the input mode toggle (Direct / Hiragana / etc.) is
replaced entirely with the APL shifting key menu. The tool menu is kept for
Properties access.

```
[APL Shifting Key]   →  Left Ctrl ☐ | Right Ctrl ☐ | Left Alt ☐ | Right Alt ☐ | Caps Lock ☐
[Tool Menu]          →  Properties | Cancel
```

The shifting key menu uses **checkmark items** (not radio buttons) since
multiple keys can be enabled simultaneously. Each item toggles independently.

### Implementation approach

Replace the input mode toggle button (`input_button_menu_` /
`input_mode_button_for_win8_`) with an APL shifting key menu button. The
existing `TipLangBarToggleButton` class supports `TF_LBMENUF_CHECKED` flags.
We change the menu items to use `kTipLangBarItemTypeChecked` instead of
`kTipLangBarItemTypeRadioChecked`.

The key change is in `TipLangBar::InitLangBar()` — instead of initializing
the input mode toggle with Japanese composition modes (Hiragana, Katakana,
etc.), we initialize it with the five APL shifting key items.

### Menu item IDs

Extend `TipLangBarCallback::ItemId` enum:

```cpp
// APL shifting key selection (50-54)
kAplShiftingKeyLeftCtrl = 50,
kAplShiftingKeyRightCtrl = 51,
kAplShiftingKeyLeftAlt = 52,
kAplShiftingKeyRightAlt = 53,
kAplShiftingKeyCapsLock = 54,
```

### Menu callback flow

1. User clicks a shifting key menu item
2. `TipLangBarToggleButton::OnMenuSelect(menu_id)` fires
3. Callback reaches `TipTextServiceImpl::OnMenuSelect(ItemId)`
4. Read current `AplShiftingKeySet` from config via Mozc client
5. Toggle the corresponding bool field
6. Write updated config back via Mozc client
7. Refresh cached config in `TipThreadContext`
8. Call `OnUpdate()` to refresh the menu checkmark state

---

Config Proto Changes
---------------------

The `AplShiftingKeySet` message in `config.proto` needs to be
platform-inclusive. Since this branch has no APL proto changes yet (they exist
only on the macOS branch), we design the message to cover both platforms:

```proto
message AplShiftingKeySet {
  // macOS (unified, no L/R distinction)
  optional bool ctrl = 1;
  optional bool option = 2;
  optional bool caps_lock = 3;    // shared: macOS + Windows

  // Windows (left/right distinguished)
  optional bool left_ctrl = 10;
  optional bool right_ctrl = 11;
  optional bool left_alt = 12;
  optional bool right_alt = 13;
  // Windows shares caps_lock field 3 with macOS
}
```

Added to `Config` message as:
```proto
optional AplShiftingKeySet apl_shifting_key_set = 122;
```

Field 122 is in the "Renderer" range (120-139) which is the same location
chosen on the macOS branch. This keeps the two branches mergeable.

### Config access on Windows

Config access on Windows uses two tiers:

1. **Startup load** — `ConfigSnapshot::Get()` (`src/win32/base/config_snapshot.cc`)
   reads config from disk once, when `TipPrivateContext` is created. This is a
   `static const` cache with no refresh mechanism — it exists for existing Mozc
   settings (kana input, mode indicator, etc.) that rarely change mid-session.

2. **Live runtime state** — `InputBehavior` (`src/win32/base/input_state.h`) is
   a mutable, per-context struct that `TipPrivateContext` owns. At startup,
   `EnsureInitialized()` copies snapshot values into it. The key event handler
   reads `InputBehavior` on every keystroke via
   `private_context->input_behavior()` — it never reads `ConfigSnapshot`
   directly.

For the APL shifting keys, we extend `InputBehavior` (not `ConfigSnapshot::Info`):

```cpp
struct InputBehavior {
  // ... existing fields ...
  bool apl_shifting_left_ctrl = false;
  bool apl_shifting_right_ctrl = false;
  bool apl_shifting_left_alt = false;
  bool apl_shifting_right_alt = false;
  bool apl_shifting_caps_lock = false;
};
```

We also extend `ConfigSnapshot::Info` and `ConfigSnapshot::Get()` so that the
initial load from disk populates these fields, which `EnsureInitialized()` then
copies into `InputBehavior`.

For writing config (when the user toggles a menu item), the TIP uses the Mozc
client interface directly — `TipPrivateContext::GetClient()` →
`GetConfig()` / `SetConfig()`, same pattern as the macOS implementation. After
`SetConfig` persists the change to disk (via the server), the menu handler also
updates `InputBehavior` directly — the new value is already known (it was just
toggled), so no IPC round-trip or cache refresh is needed. This gives immediate
effect for the key intercept while ensuring persistence across restarts.

---

Physical Key Codes (Windows vs macOS)
--------------------------------------

Windows uses **Win32 virtual key codes** (`WPARAM` in `OnKeyDown`). These are
layout-independent for letter/number keys (e.g., `VK_A = 0x41` regardless of
keyboard layout) but layout-dependent for symbol keys.

| Concern | macOS | Windows |
|---------|-------|---------|
| Physical key identifier | Carbon vkey (`[event keyCode]`) | Win32 VK code (`WPARAM wparam`) |
| Letter keys | `kVK_ANSI_A = 0x00` (non-sequential) | `VK_A = 0x41` ('A' ASCII, sequential) |
| Layout independence | Fully layout-independent | Letters/digits: yes. Symbols: use scan code from `LPARAM` |
| Modifier detection | `[event modifierFlags]` bitmask | `GetKeyState(VK_LCONTROL)` etc. per-key query |
| L/R modifiers | Not reliably available | Full support via `VK_LCONTROL`/`VK_RCONTROL` etc. |

Step W5 introduces `Win32VKToChar(WPARAM vk)` — analogous to the macOS
`AplVirtualKeyToChar`. The Windows version is simpler for letters (VK_A–VK_Z
map directly to 'a'–'z') but needs a lookup table for `VK_OEM_*` symbol keys.
For the POC, only US QWERTY layout is supported; non-US layout support (using
scan codes from `LPARAM`) is out of scope.

---

Differences from macOS Implementation
---------------------------------------

| Concern | macOS (IMK) | Windows (TSF) |
|---------|-------------|---------------|
| Event entry point | `handleEvent:client:` | `OnTestKeyDown` / `OnKeyDown` |
| Event dispatch | Synchronous | Synchronous |
| L/R modifier distinction | No | Yes (`VK_LCONTROL` vs `VK_RCONTROL`) |
| Caps Lock interception | Stripped in `KeyCodeMap.mm`; intercept before | TSF delivers before toggle; eat to suppress |
| Alt/Option bare-press | No side effect on macOS | Activates menu bar on Windows |
| Mode registration | `Info.plist` `ComponentInputModeDict` | `AddLanguageProfile` with language ID |
| Language bar / menu | NSMenu with `IBAction` checkmark items | `ITfLangBarItemButton` with `TF_LBMENUF_CHECKED` |
| Config access | `mozcClient_->GetConfig()` (direct) | `InputBehavior` (live) + `ConfigSnapshot` (initial load) + client `GetConfig`/`SetConfig` (persistence) |
| Installer | macOS pkg (not covered) | WiX MSI with custom actions |

---

Phase 1 Plan: Windows POC
===========================

**Goal**: Mozc installs as an English-only "Mozc APL" input method on Windows.
The system tray shows an "APL Shifting Key" menu with five checkable modifier
options. Toggling items persists to config and survives IME restarts. Holding
a configured shifting key while pressing a character key inserts the
corresponding APL glyph.

**Baseline**: Current `windows-poc` branch (from `master`). No APL-related
code exists yet.

### Implementation order rationale

Following the macOS experience, we build incrementally. Each step is
independently testable. The risk profile is:

- W1 is proto changes (config + CompositionMode) — testable via build success
- W2 is a registration change — testable via install + Settings UI
- W3 is language bar menu — visible in system tray, no behavioral change
- W4 is config wiring — menu toggles persist, no key handling changes
- W5 is glyph production — behavioral change (key intercept + text insertion)
- W6 is installer verification — full install/uninstall cycle

W5 comes after W4 because the key intercept reads the cached config fields
(which shifting key is active) that W4 wires up. W6 is last because the
installer is a "full stack" smoke test — it should verify the complete
feature, including glyph production.

### Step W1: Proto changes (no behavioral change)

**1a. Config proto** — Add the `AplShiftingKeySet` message and field 122 to
`config.proto`:

```proto
message AplShiftingKeySet {
  optional bool ctrl = 1;         // macOS unified Ctrl
  optional bool option = 2;       // macOS Option (Alt)
  optional bool caps_lock = 3;    // shared: macOS + Windows

  optional bool left_ctrl = 10;   // Windows L-Ctrl
  optional bool right_ctrl = 11;  // Windows R-Ctrl
  optional bool left_alt = 12;    // Windows L-Alt
  optional bool right_alt = 13;   // Windows R-Alt
}
```

On `Config`:
```proto
optional AplShiftingKeySet apl_shifting_key_set = 122;
```

**1b. CompositionMode** — Repurpose the Japanese-only modes for APL. In
`commands.proto`, replace the unused Japanese modes:

```proto
enum CompositionMode {
  DIRECT = 0;
  HIRAGANA = 1;       // unused in APL POC, kept for value stability
  APL = 2;            // was FULL_KATAKANA
  HALF_ASCII = 3;
  FULL_ASCII = 4;
  // HALF_KATAKANA (5) removed
  NUM_OF_COMPOSITIONS = 6;  // unchanged
}
```

This avoids adding `APL = 6` (as the macOS branch did) and the cascade of
array-size changes that would follow. Safe for a throwaway POC that will
never merge with the macOS branch. `HIRAGANA` and the ASCII modes are kept
to avoid breaking code that references them by value — they just become
dead paths that the APL TIP never enters.

**Files changed**:
- `src/protocol/config.proto`
- `src/protocol/commands.proto`

**Test**: Build succeeds. Proto generates correctly. Existing Mozc behavior
unaffected (config field is optional, CompositionMode values 0/1/3/4 are
unchanged).

### Step W2: English registration (replace Japanese with APL)

Change the existing profile from Japanese to English and update the display
name. This is the minimal change set to make Mozc appear as an English input
method.

**Files changed**:
- `src/win32/base/tsf_profile.cc` — change `kTextServiceLanguage` from
  `MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT)` to
  `MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT)`
- `src/win32/tip/tip_resource.rc` — change display name strings from `"Mozc"`
  to `"Mozc APL"` (both `IDS_IME_DISPLAYNAME` and
  `IDS_TEXTSERVICE_DISPLAYNAME_SYNONYM`)
- `src/win32/custom_action/custom_action.cc` — change `0x0411` to `0x0409` in
  `EnableTipProfile`

**Test**: Build, install, open Windows Settings → Time & Language → Language →
English → Keyboard. "Mozc APL" appears as an available input method. The
system tray input indicator shows "Mozc APL" when selected. Switching to
Mozc APL activates the TIP (it will show the existing Japanese menus for now —
that's expected and will be replaced in W3).

### Step W3: APL shifting key language bar menu (UI only)

Replace the Japanese input mode menu with the APL shifting key menu.

**Why `TipLangBarMenuButton`, not `TipLangBarToggleButton`**:
The existing `TipLangBarToggleButton` hard-codes radio-select semantics — a
single `menu_selected_` index, `TF_LBMENUF_RADIOCHECKED` flags, and
`SelectMenuItem` that clears all other items when one is selected. Converting
it to multi-select would require gutting its selection model.

`TipLangBarMenuButton` has no selection model at all. Its `InitMenu` reads
`flags_` directly from each `TipLangBarMenuData` entry, and its `OnMenuSelect`
just delegates to the callback. This makes multi-select trivial: set or clear
`TF_LBMENUF_CHECKED` on the item's `flags_` field, then call `OnUpdate` to
refresh the menu.

**Files changed**:
- `src/win32/tip/tip_lang_bar_callback.h` — add `kAplShiftingKeyLeftCtrl`
  through `kAplShiftingKeyCapsLock` to `ItemId` enum (IDs 50-54)
- `src/win32/tip/tip_lang_bar.h` — replace `input_button_menu_` and
  `input_mode_button_for_win8_` (both `TipLangBarToggleButton`) with a single
  `apl_shifting_button_` of type `TipLangBarMenuButton`
- `src/win32/tip/tip_lang_bar.cc`:
  - Replace input mode menu item arrays with APL shifting key items, using
    `kTipLangBarItemTypeDefault` (unchecked initially)
  - Remove Japanese composition mode items (Direct, Hiragana, etc.)
  - `UpdateMenu()` sets `TF_LBMENUF_CHECKED` on each item's `flags_` field
    based on the current shifting key state, then calls `OnUpdate`
- `src/win32/tip/tip_text_service.cc`:
  - `OnMenuSelect` callback: for APL item IDs, toggle the item's
    `TF_LBMENUF_CHECKED` flag directly on its `TipLangBarMenuData::flags_`
    (stub: no config persistence yet, just visual toggle)
  - Remove or stub out Japanese input mode switch logic in `OnMenuSelect`
  - `UpdateLangbar()`: adapted for checkmark-based menu

**Resource strings** (in `tip_resource.rc`):
- Add string IDs: `IDS_APL_SHIFTING_LEFT_CTRL`, `IDS_APL_SHIFTING_RIGHT_CTRL`,
  `IDS_APL_SHIFTING_LEFT_ALT`, `IDS_APL_SHIFTING_RIGHT_ALT`,
  `IDS_APL_SHIFTING_CAPS_LOCK` with values "Left Ctrl", "Right Ctrl",
  "Left Alt", "Right Alt", "Caps Lock"

**Menu structure**:
```
APL Shifting Key ▸
  ☐ Left Ctrl
  ☐ Right Ctrl
  ☐ Left Alt
  ☐ Right Alt
  ☐ Caps Lock
```

**Checkmark toggle flow** (W3 is UI-only; config persistence is added in W4):
```
User clicks "Right Ctrl"
  → TipLangBarMenuButton::OnMenuSelect(index)
    → callback->OnMenuSelect(kAplShiftingKeyRightCtrl)
      → toggle TF_LBMENUF_CHECKED on that item's TipLangBarMenuData::flags_
      → apl_shifting_button_->OnUpdate(TF_LBI_STATUS)
  → next InitMenu call reads updated flags_, shows checkmark
```

**Test**: Install, switch to Mozc APL. Click the system tray icon. The "APL
Shifting Key" menu appears with five items. Clicking items toggles checkmarks
(visual only for now — no config persistence yet). Multiple items can be
checked simultaneously. The old Japanese input mode menu (Direct / Hiragana /
etc.) no longer appears.

### Step W4: Config wiring (shifting key persistence)

Wire the menu toggles to read/write `config.proto` field 122 via the Mozc
server.

**Files changed**:
- `src/win32/base/input_state.h` — add APL shifting key fields to
  `InputBehavior`
- `src/win32/base/config_snapshot.h` — add APL shifting key fields to `Info`
  (for initial load only)
- `src/win32/base/config_snapshot.cc` — populate from
  `config.apl_shifting_key_set()`
- `src/win32/tip/tip_private_context.cc` — `EnsureInitialized()` copies
  snapshot APL fields into `InputBehavior`
- `src/win32/tip/tip_text_service.cc` — `OnMenuSelect` for APL items: read
  config via client → toggle field → write config → update `InputBehavior`
  directly → update menu checkmarks
- `src/win32/tip/tip_lang_bar.cc` — `UpdateMenu` reads `InputBehavior` to set
  checkmark state on each item

**Config read path** (startup):
```
TipPrivateContext::EnsureInitialized()
  → ConfigSnapshot::Get(&snapshot)         // one-shot read from disk
    → config.apl_shifting_key_set().left_ctrl() etc.
    → populate snapshot.apl_shifting_* fields
  → behavior->apl_shifting_left_ctrl = snapshot.apl_shifting_left_ctrl  // copy into live state
  → ... (same for all five fields)
  → TipLangBar::UpdateMenu() sets checkmark flags from InputBehavior
```

**Config write path** (menu toggle):
```
User clicks menu item
  → OnMenuSelect(kAplShiftingKeyRightCtrl)
    → client->GetConfig(&config)
    → config.mutable_apl_shifting_key_set()->set_right_ctrl(!current)
    → client->SetConfig(config)                              // persist to disk via server
    → private_context->mutable_input_behavior()
        ->apl_shifting_right_ctrl = !current                 // immediate update to live state
    → TipLangBar::UpdateMenu() refreshes checkmarks from InputBehavior
```

Note: `ConfigSnapshot::Get()` is only used for the initial load — it is a
`static const` cache with no refresh mechanism. All runtime reads go through
`InputBehavior`, which the menu handler updates directly after `SetConfig`.
This mirrors the macOS approach where `handleConfig` updates ivars
(`aplShiftingKeyCtrl_` etc.) immediately after `SetConfig`.

**Test**: Install, switch to Mozc APL. Toggle "Right Ctrl" in the menu. Close
and reopen the menu — checkmark persists. Restart the IME (switch away and
back, or restart the app) — checkmark still persists. Toggle multiple items
simultaneously (e.g., Right Ctrl + Caps Lock both checked).

### Step W5: APL glyph production (behavioral change)

Intercept key events when a configured shifting key is held, look up the APL
glyph, and insert it directly — consuming the keystroke before the application
sees it.

**5a. Virtual key → character mapping** — New file `src/win32/tip/win32_vk_to_char.h`:

```cpp
// Returns the US QWERTY character for a Win32 virtual key code.
// Letters (VK_A–VK_Z) map to 'a'–'z'. Digits (VK_0–VK_9) map to '0'–'9'.
// Symbol keys (VK_OEM_*) map to their unshifted US QWERTY character.
// Returns '\0' for unmapped keys (function keys, modifiers, etc.).
char Win32VKToChar(WPARAM vk);

// Returns the US QWERTY shifted character for a base character.
// 'a'→'A', '1'→'!', '['→'{', etc.
char ShiftedChar(char base);
```

This is the Windows equivalent of `src/mac/apl_keycode_map.h`. Simpler for
letters/digits (sequential VK codes map directly) but needs a table for
`VK_OEM_*` symbol keys. `ShiftedChar` is the same logic as the macOS version.

**5b. Key intercept in `OnTestKeyDown` / `OnKeyDown`** — The intercept goes
in `TipKeyeventHandler` (or a new helper called from it) and runs before the
existing Mozc key processing:

```
OnKeyDown(vk, lParam):
  if no shifting key configured → fall through to normal Mozc path
  if shifting key is held (GetKeyState check):
    base_char = Win32VKToChar(vk)
    if base_char == '\0' → fall through (non-character key)
    if Shift is also held:
      shifted = ShiftedChar(base_char)
      glyph = GetAplShiftedGlyph(shifted)
    else:
      glyph = GetAplGlyph(base_char)
    if glyph found:
      insert glyph via ITfInsertAtSelection / ITfRange
      return eaten=TRUE
    fall through (key has no APL mapping)
  fall through to normal Mozc path
```

The `GetAplGlyph` / `GetAplShiftedGlyph` functions are cherry-picked from
`src/session/apl_keymap.h` (same as macOS M5).

**Modifier detection** — Check which shifting key is held, reading from the
live `InputBehavior` (available via `private_context->input_behavior()` in the
key event handler):

```cpp
bool IsShiftingKeyHeld(const InputBehavior& behavior) {
  if (behavior.apl_shifting_left_ctrl  && (GetKeyState(VK_LCONTROL) & 0x8000)) return true;
  if (behavior.apl_shifting_right_ctrl && (GetKeyState(VK_RCONTROL) & 0x8000)) return true;
  if (behavior.apl_shifting_left_alt   && (GetKeyState(VK_LMENU)   & 0x8000)) return true;
  if (behavior.apl_shifting_right_alt  && (GetKeyState(VK_RMENU)   & 0x8000)) return true;
  if (behavior.apl_shifting_caps_lock  && (GetKeyState(VK_CAPITAL)  & 0x0001)) return true;
  return false;
}
```

Note: Caps Lock uses `& 0x0001` (toggle state / LED on) rather than
`& 0x8000` (key currently pressed). This matches the Kanata-style usage where
Caps Lock is a latching shift — turn it on, type APL glyphs, turn it off.

**5c. Caps Lock toggle suppression** — When Caps Lock is a shifting key and
APL mode is active, the TIP must eat `VK_CAPITAL` keydown in `OnTestKeyDown`
to prevent the system from toggling the LED. This lets the user control Caps
Lock state deliberately (e.g., via the physical key when APL is not active)
while preventing accidental toggles during APL use.

However, for the POC the simpler approach is to **let the LED toggle freely**
and just read the toggle state. The user turns Caps Lock on to enter APL
shifting mode, types glyphs, and turns it off when done. The LED serves as a
visual indicator. This avoids the complexity of eating `VK_CAPITAL` while
still allowing APL input through `OnTestKeyDown`.

**5d. Left Alt bare-press suppression** — When Left Alt is a shifting key,
releasing Alt without a character key press would activate the menu bar in
Win32 apps. The TIP suppresses this by eating `VK_LMENU` keyup in `OnKeyUp`
when APL mode is active and Left Alt is configured as a shifting key. This
matches the Linux branch's approach.

**5e. Text insertion** — TSF text insertion uses `ITfInsertAtSelection`:

```cpp
// Simplified — real code needs ITfContext, ITfEditSession, etc.
void InsertAplGlyph(ITfContext* context, const wchar_t* glyph) {
  // Request an edit session
  // In the session: ITfInsertAtSelection::InsertTextAtSelection(
  //   ec, context, TF_IAS_NOQUERY, glyph, wcslen(glyph), nullptr);
}
```

The glyph strings from `apl_keymap.h` are UTF-8 `string_view`; they need
conversion to UTF-16 (`wchar_t`) for TSF. Since APL glyphs are all BMP
characters (U+0000–U+FFFF), each is a single `wchar_t` — no surrogate pairs.

**Files changed**:
- `src/win32/tip/win32_vk_to_char.h` (new) — VK code → char mapping
- `src/win32/tip/win32_vk_to_char.cc` (new) — implementation
- `src/session/apl_keymap.h` (cherry-pick from macOS/Linux branch)
- `src/session/apl_keymap.cc` (cherry-pick from macOS/Linux branch)
- `src/session/BUILD.bazel` — add `apl_keymap` rule
- `src/win32/tip/BUILD.bazel` — add `win32_vk_to_char` rule, add
  `//session:apl_keymap` dep to TIP
- `src/win32/tip/tip_keyevent_handler.cc` — APL intercept before normal
  Mozc key processing
- `src/win32/tip/tip_text_service.cc` — text insertion helper, edit session
  management

**Test**: Install, switch to Mozc APL. Enable "Right Ctrl" in the shifting
key menu. Open Notepad. Hold Right Ctrl and press `a` — the APL glyph `⍺`
(alpha) appears. Release Right Ctrl, press `a` — normal `a` appears. Test
with Shift held: Right Ctrl + Shift + `a` → `⍶` (alpha underbar). Test
multiple shifting keys: enable both Right Ctrl and Caps Lock, verify both
produce glyphs independently.

### Step W6: Installer verification

Verify the WiX installer properly registers and enables the APL profile with
the English language ID and that glyph production works end-to-end.

**Files changed**:
- None beyond W2 changes. The installer custom actions (`RegisterTIP`,
  `EnableTipProfile`) call into `TsfRegistrar` and `TsfProfile` which were
  already updated in W2.

**Test**: Clean Windows VM. Run the MSI installer. Open Settings → Language →
English → Keyboard. "Mozc APL" appears. Switch to Mozc APL — shifting key
menu appears with correct checkmark state. Toggle items, restart, verify
persistence. Enable a shifting key, open Notepad, hold the key and type —
APL glyphs appear. Uninstall — profile removed cleanly, no orphaned registry
entries.

---

Open Questions (Require Experimental Testing)
----------------------------------------------

### Q1: Does TSF deliver VK_CAPITAL before the Caps Lock toggle?

**Hypothesis**: YES. TSF key events arrive synchronously before the key's
default action. Returning `eaten=TRUE` from `OnTestKeyDown` should prevent
the Caps Lock LED and state from toggling.

**Test** (W5): Register `VK_CAPITAL` as a preserved key or handle it in
`OnTestKeyDown`. Press Caps Lock — verify the LED does not toggle and the
key is consumed.

### Q2: Can the TIP distinguish L/R modifiers reliably?

**Hypothesis**: YES. `GetKeyState(VK_LCONTROL)` and `GetKeyState(VK_RCONTROL)`
are standard Win32 APIs and should work from within a TSF key event handler.
The `LPARAM` scan code also encodes the extended-key bit (bit 24) which
distinguishes right-side keys.

**Test** (W5): In `OnKeyDown`, call `GetKeyState(VK_LCONTROL)` and
`GetKeyState(VK_RCONTROL)` — verify they report independently.

### Q3: Does eating Ctrl+A prevent "select all" in Notepad / VSCode?

**Hypothesis**: YES for Notepad (Win32 app, TSF is authoritative). For
VSCode (Electron/Chromium), needs testing — Chromium has its own keyboard
handling that may race with TSF.

**Test** (W5): With APL mode active and R-Ctrl as shifting key, press
R-Ctrl+A. Expected: glyph inserted, not "select all".

### Q4: Does the Mozc server need to be running for GetConfig/SetConfig?

**Hypothesis**: YES. The Mozc client (`client::Client`) communicates with
`mozc_server.exe` via IPC. If the server is not running, `GetConfig` will
fail. The server is auto-started by `mozc_broker.exe` at login (installed
as a Run key). For the POC this is fine since we assume a full Mozc install.

---

Key Files (Windows)
--------------------

### Existing files to modify

| File | Role | Changes |
|------|------|---------|
| `src/protocol/config.proto` | Shared config proto | Add `AplShiftingKeySet` message, field 122 |
| `src/win32/base/tsf_profile.cc` | Profile language ID | Change to `LANG_ENGLISH` |
| `src/win32/tip/tip_resource.rc` | Display name strings | Change to "Mozc APL" |
| `src/win32/custom_action/custom_action.cc` | Installer actions | Change `0x0411` to `0x0409` |
| `src/win32/tip/tip_lang_bar_callback.h` | Menu item IDs | Add APL shifting key item IDs (50-54) |
| `src/win32/tip/tip_lang_bar.h` | Language bar manager | Replace input mode button with APL shifting button |
| `src/win32/tip/tip_lang_bar.cc` | Language bar init/update | APL shifting key menu items, checkmark semantics |
| `src/win32/tip/tip_text_service.cc` | Main TIP class | APL menu handling in `OnMenuSelect`, text insertion helper |
| `src/win32/base/input_state.h` | Live config state | Add APL shifting key fields to `InputBehavior` |
| `src/win32/base/config_snapshot.h` | Startup config cache | Add APL shifting key fields to `Info` (initial load only) |
| `src/win32/base/config_snapshot.cc` | Startup cache population | Read from `apl_shifting_key_set()` |
| `src/win32/tip/tip_private_context.cc` | Per-context init | Copy snapshot APL fields into `InputBehavior` |
| `src/win32/tip/tip_keyevent_handler.cc` | Key event processing | APL intercept before normal Mozc path |

### New files

| File | Role | Step |
|------|------|------|
| `src/win32/tip/win32_vk_to_char.h` | Win32 VK → US QWERTY char mapping | W5 |
| `src/win32/tip/win32_vk_to_char.cc` | Implementation | W5 |
| `src/session/apl_keymap.h` | APL glyph lookup (cherry-pick from macOS/Linux) | W5 |
| `src/session/apl_keymap.cc` | Implementation (cherry-pick from macOS/Linux) | W5 |

---

Known Risks and Mitigations
=============================

**Risk**: Changing the language from Japanese to English may break assumptions
elsewhere in the TIP code (e.g., `EnsureKanaLockUnlocked()` in `ActivateEx`,
Japanese-specific keyboard handling in `TipKeyeventHandler`).

*Mitigation*: Before changing the language ID, perform an explicit audit of
every Japanese-specific site in `src/win32/`. Classify each as (a) already
being changed by this POC, (b) truly harmless no-op under English, or
(c) needs a guard or early-return to disable safely.

**Audit checklist** (grep `src/win32/` for `0x0411`, `LANG_JAPANESE`,
`VK_KANA`, `kana`, `Kana`):

| Site | File | Classification |
|------|------|----------------|
| `kTextServiceLanguage` constant | `src/win32/base/tsf_profile.cc:74` | (a) changed to `LANG_ENGLISH` in W1 |
| `"0x0411:"` in `InstallLayoutOrTip` | `src/win32/custom_action/custom_action.cc:327` | (a) changed to `"0x0409:"` in W1 |
| `"0x0411:"` in `InstallLayoutOrTip` | `src/win32/base/imm_util.cc:66` | (a) changed to `"0x0409:"` in W1 |
| `TsfProfile::GetLangId()` in `RegisterProfile`/`UnregisterProfile` | `src/win32/base/tsf_registrar.cc:193,222` | (a) reads `kTextServiceLanguage`, picks up W1 change |
| `EnsureKanaLockUnlocked()` in `ActivateEx` and `OnSetThreadFocus` | `src/win32/tip/tip_text_service.cc:233-239,509,731` | (b) clears `VK_KANA` via `GetKeyboardState`/`SetKeyboardState` — works on all systems, no-op when kana is already unlocked |
| `IsKanaLocked()` virtual method | `src/win32/base/keyboard.h:128`, `keyboard.cc:90` | (b) returns false on English keyboards — no effect |
| `IsPressed(VK_KANA)` in modifier index | `src/win32/base/keyboard.cc:1324` | (b) never pressed on English keyboards — no effect |
| Hiragana/Katakana conversion modes | `src/win32/base/conversion_mode_util.cc` | (b) language-agnostic logic, no language ID check |

No category (c) sites have been identified. All Japanese-specific code
either gets updated by the language ID change (a) or is a genuine no-op
under English (b). Dead code cleanup is deferred to post-POC.

**Risk**: `ConfigSnapshot::Get()` is a `static const` one-shot cache — it
cannot reflect config changes made after startup.

*Mitigation*: The key event handler never reads `ConfigSnapshot` directly.
It reads `InputBehavior`, a mutable per-context struct owned by
`TipPrivateContext`. `ConfigSnapshot` is used only to populate
`InputBehavior` at startup (in `EnsureInitialized()`). After a menu toggle,
the handler updates `InputBehavior` directly — the new value is already
known (it was just toggled), so no IPC round-trip or cache refresh is
needed. `SetConfig` persists the change to disk for cross-restart durability.
This mirrors the macOS approach where ivars are updated immediately after
`SetConfig`.

**Risk**: The Mozc server may not be running when the APL profile is
activated (e.g., server crashed or was not started).

*Mitigation*: `GetConfig`/`SetConfig` failures are non-fatal. The menu
shows unchecked items as default. The broker auto-restarts the server.

**Risk**: Caps Lock is a toggle key on Windows — pressing it changes
persistent LED state. When used as an APL shifting key, the key intercept
in W5 must handle this.

*Design note (W5)*: The APL key intercept reads toggle state via
`GetKeyState(VK_CAPITAL) & 0x0001` (LED on = APL shift active), not the
`IsPressed(VK_CAPITAL)` check that the existing keyevent handler uses
(line 431 of `keyevent_handler.cc`). The POC lets the LED toggle freely
and uses it as a visual indicator — the user turns Caps Lock on to enter
APL shifting mode and off when done. Eating `VK_CAPITAL` to suppress
the toggle is deferred as a refinement. The intercept must happen before
`TipKeyeventHandler` processes the key, since the existing handler at
`keyevent_handler.cc:460` would otherwise forward `VK_CAPITAL` to the
Mozc server as `KeyEvent::CAPS`. Note: Caps Lock was abandoned as a
shifting key option on the macOS branch; however it is actively used on
Windows via Kanata and must work in this POC.


