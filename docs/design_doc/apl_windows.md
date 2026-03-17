APL Shifting Key — Windows / TSF Proof of Concept
===================================================

This document covers the Windows-specific proof of concept for APL shifting-key
input using Microsoft's Text Services Framework (TSF). For the macOS
implementation see [apl_macos.md](apl_macos.md).

---

Scope
-----

This POC validates three things:

1. **Installation**: Mozc registers as an **English** input method on Windows
   via TSF, with no Japanese profile.
2. **System tray menu**: The language bar exposes an "APL Shifting Key" submenu
   with checkable items for each modifier key.
3. **Config persistence**: The user's shifting key selection round-trips through
   `config.proto` via the Mozc server and survives IME restarts.

**Out of scope for this POC**: Actual APL glyph insertion (the `handleEvent`
intercept equivalent), APL mode toggling, virtual key → char mapping, and
cross-app testing. Those will follow in a subsequent phase once the foundation
is validated.

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
is the shifting key. This is a future concern (glyph insertion phase), not the
POC.

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

The existing `ConfigSnapshot` class (`src/win32/base/config_snapshot.cc`)
caches select config fields for the TIP. For the POC, we extend it to include
the `AplShiftingKeySet`:

```cpp
struct ConfigSnapshot::Info {
  // ... existing fields ...
  bool apl_shifting_left_ctrl;
  bool apl_shifting_right_ctrl;
  bool apl_shifting_left_alt;
  bool apl_shifting_right_alt;
  bool apl_shifting_caps_lock;
};
```

For writing config (when the user toggles a menu item), the TIP uses the Mozc
client interface directly — `TipPrivateContext::GetClient()` →
`GetConfig()` / `SetConfig()`, same pattern as the macOS implementation.

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

For the glyph insertion phase (not this POC), we will need a
`Win32VKToChar(WPARAM vk, LPARAM lParam)` function analogous to the macOS
`AplVirtualKeyToChar`. The Windows version is simpler for letters (VK_A-VK_Z
map directly to 'a'-'z') but needs scan code extraction from LPARAM for symbol
keys to handle non-US layouts.

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
| Config access | `mozcClient_->GetConfig()` (direct) | `ConfigSnapshot` cache + client `GetConfig`/`SetConfig` |
| Installer | macOS pkg (not covered) | WiX MSI with custom actions |

---

Phase 1 Plan: Windows POC
===========================

**Goal**: Mozc installs as an English-only "Mozc APL" input method on Windows.
The system tray shows an "APL Shifting Key" menu with five checkable modifier
options. Toggling items persists to config and survives IME restarts.

**Baseline**: Current `windows-poc` branch (from `master`). No APL-related
code exists yet.

### Implementation order rationale

Following the macOS experience, we build incrementally. Each step is
independently testable. The risk profile is:

- W1 is a proto change — testable via build success
- W2 is a registration change — testable via install + Settings UI
- W3 is language bar menu — visible in system tray, no behavioral change
- W4 is config wiring — menu toggles persist, no key handling changes
- W5 is installer verification — full install/uninstall cycle

No key interception or glyph insertion in this POC. The behavioral changes
arrive in a separate phase after the foundation is validated.

### Step W1: Proto changes (shared, no behavioral change)

Add the `AplShiftingKeySet` message and field 122 to `config.proto`:

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

**Files changed**:
- `src/protocol/config.proto`

**Test**: Build succeeds. Proto generates correctly. Existing Mozc behavior
unaffected (field is optional with no default — absent by default).

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

**Files changed**:
- `src/win32/tip/tip_lang_bar_callback.h` — add `kAplShiftingKeyLeftCtrl`
  through `kAplShiftingKeyCapsLock` to `ItemId` enum (IDs 50-54)
- `src/win32/tip/tip_lang_bar.h` — replace `input_button_menu_` and
  `input_mode_button_for_win8_` with `apl_shifting_button_`
- `src/win32/tip/tip_lang_bar.cc`:
  - Replace input mode menu item arrays with APL shifting key items
  - Use `kTipLangBarItemTypeChecked` instead of
    `kTipLangBarItemTypeRadioChecked`
  - Remove Japanese composition mode items (Direct, Hiragana, etc.)
  - `UpdateMenu()` sets checkmark state per item instead of radio selection
- `src/win32/tip/tip_text_service.cc`:
  - `OnMenuSelect`: add cases for `kAplShiftingKeyLeftCtrl` through
    `kAplShiftingKeyCapsLock` (stub: log selection for now)
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

**Test**: Install, switch to Mozc APL. Click the system tray icon. The "APL
Shifting Key" menu appears with five items. Clicking items toggles checkmarks
(visual only for now — no config persistence yet). The old Japanese input mode
menu (Direct / Hiragana / etc.) no longer appears.

### Step W4: Config wiring (shifting key persistence)

Wire the menu toggles to read/write `config.proto` field 122 via the Mozc
server.

**Files changed**:
- `src/win32/base/config_snapshot.h` — add APL shifting key fields to `Info`
- `src/win32/base/config_snapshot.cc` — populate from
  `config.apl_shifting_key_set()`
- `src/win32/tip/tip_text_service.cc` — `OnMenuSelect` for APL items: read
  config via client → toggle field → write config → refresh cache → update
  menu checkmarks
- `src/win32/tip/tip_lang_bar.cc` — `UpdateMenu` reads cached config to set
  checkmark state on each item

**Config read path**:
```
TipTextService::ActivateEx()
  → ConfigSnapshot::Get()
    → client->GetConfig()
      → config.apl_shifting_key_set().left_ctrl() etc.
    → populate Info.apl_shifting_* fields
  → TipLangBar::UpdateMenu() sets checkmark flags
```

**Config write path**:
```
User clicks menu item
  → OnMenuSelect(kAplShiftingKeyRightCtrl)
    → client->GetConfig(&config)
    → config.mutable_apl_shifting_key_set()->set_right_ctrl(!current)
    → client->SetConfig(config)
    → refresh ConfigSnapshot cache
    → TipLangBar::UpdateMenu() refreshes checkmarks
```

**Test**: Install, switch to Mozc APL. Toggle "Right Ctrl" in the menu. Close
and reopen the menu — checkmark persists. Restart the IME (switch away and
back, or restart the app) — checkmark still persists. Toggle multiple items
simultaneously (e.g., Right Ctrl + Caps Lock both checked).

### Step W5: Installer verification

Verify the WiX installer properly registers and enables the APL profile with
the English language ID.

**Files changed**:
- None beyond W2 changes. The installer custom actions (`RegisterTIP`,
  `EnableTipProfile`) call into `TsfRegistrar` and `TsfProfile` which were
  already updated in W2.

**Test**: Clean Windows VM. Run the MSI installer. Open Settings → Language →
English → Keyboard. "Mozc APL" appears. Switch to Mozc APL — shifting key
menu appears with correct checkmark state. Toggle items, restart, verify
persistence. Uninstall — profile removed cleanly, no orphaned registry entries.

---

Open Questions (Require Experimental Testing)
----------------------------------------------

### Q1: Does TSF deliver VK_CAPITAL before the Caps Lock toggle?

**Hypothesis**: YES. TSF key events arrive synchronously before the key's
default action. Returning `eaten=TRUE` from `OnTestKeyDown` should prevent
the Caps Lock LED and state from toggling.

**Test** (future glyph phase): Register `VK_CAPITAL` as a preserved key or
handle it in `OnTestKeyDown`. Press Caps Lock — verify the LED does not
toggle and the key is consumed.

### Q2: Can the TIP distinguish L/R modifiers reliably?

**Hypothesis**: YES. `GetKeyState(VK_LCONTROL)` and `GetKeyState(VK_RCONTROL)`
are standard Win32 APIs and should work from within a TSF key event handler.
The `LPARAM` scan code also encodes the extended-key bit (bit 24) which
distinguishes right-side keys.

**Test** (future glyph phase): In `OnKeyDown`, call `GetKeyState(VK_LCONTROL)`
and `GetKeyState(VK_RCONTROL)` — verify they report independently.

### Q3: Does eating Ctrl+A prevent "select all" in Notepad / VSCode?

**Hypothesis**: YES for Notepad (Win32 app, TSF is authoritative). For
VSCode (Electron/Chromium), needs testing — Chromium has its own keyboard
handling that may race with TSF.

**Test** (future glyph phase): With APL mode active and R-Ctrl as shifting
key, press R-Ctrl+A. Expected: glyph inserted, not "select all".

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
| `src/win32/tip/tip_text_service.cc` | Main TIP class | APL menu handling in `OnMenuSelect`, remove Japanese mode switching |
| `src/win32/base/config_snapshot.h` | Config cache | Add APL shifting key fields |
| `src/win32/base/config_snapshot.cc` | Config cache population | Read from `apl_shifting_key_set()` |

### New files (none for POC)

No new files are needed for the POC. All changes extend or modify existing code.

---

Known Risks and Mitigations
=============================

**Risk**: Changing the language from Japanese to English may break assumptions
elsewhere in the TIP code (e.g., `EnsureKanaLockUnlocked()` in `ActivateEx`,
Japanese-specific keyboard handling in `TipKeyeventHandler`).

*Mitigation*: For the POC, these Japanese-specific code paths become no-ops
or irrelevant. `EnsureKanaLockUnlocked()` is harmless on an English system.
The key event handler will still function — it just won't receive Japanese
composition requests. Dead code can be cleaned up later.

**Risk**: `ConfigSnapshot` is loaded once and cached. Menu toggles may not
refresh the cache immediately.

*Mitigation*: After `SetConfig`, explicitly re-read `ConfigSnapshot` or
update the cached fields directly (same approach as macOS `handleConfig`
refresh).

**Risk**: The Mozc server may not be running when the APL profile is
activated (e.g., server crashed or was not started).

*Mitigation*: `GetConfig`/`SetConfig` failures are non-fatal. The menu
shows unchecked items as default. The broker auto-restarts the server.

**Risk**: The `TipLangBarToggleButton` class uses radio-button semantics
(single selection via `menu_selected_` index). Converting to multi-select
checkmarks may require changes to `SelectMenuItem` and `InitMenu`.

*Mitigation*: The `TF_LBMENUF_CHECKED` flag is already supported by the
class. The main change is removing the mutual exclusion logic in
`SelectMenuItem` and storing per-item checked state instead of a single
selected index. If this proves too invasive, a simpler approach is to bypass
`SelectMenuItem` entirely and manage checkmark state directly in `InitMenu`
by reading the cached config.
