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

Phase 1 Plan: Windows POC (Revised)
======================================

> **Revision note (2026-03-24)**: This replaces the original plan. The first
> attempt (on `windows-poc` branch, commits `8b1bbd24f`–`cc1ca8d09`) broke
> the build by modifying `commands.proto` (`FULL_KATAKANA→APL`, removing
> `HALF_KATAKANA`), which cascaded into ~15 files across all platforms. This
> revision **leaves `commands.proto` untouched** — the APL shifting key
> feature bypasses the Mozc composition pipeline entirely (it reads
> `InputBehavior` fields and inserts glyphs directly). This eliminates the
> cascade entirely.
>
> Other improvements:
> - File logging (`c:\tmp\mozc_apl_debug.log`) at every step for verification
> - Exhaustive cross-reference of all code sites affected by each change
> - ~20 files total (vs. 28 in the previous attempt)

**Goal**: Mozc installs as an English-only "Mozc APL" input method on Windows.
The system tray shows an "APL Shifting Key" menu with five checkable modifier
options. Toggling items persists to config and survives IME restarts. Holding
a configured shifting key while pressing a character key inserts the
corresponding APL glyph.

**Baseline**: Current `master` branch. No APL-related code exists.

### Debug logging

All steps use a shared `AplLog()` utility introduced in W3. It writes to
`c:\tmp\mozc_apl_debug.log` (append mode) and also calls
`OutputDebugStringA` for DebugView. Each log line is prefixed with the step
(e.g., `W3:`, `W4:`, `W5:`) so that verification can grep for specific
steps.

New file `src/win32/tip/apl_log.h`:

```cpp
#ifndef MOZC_WIN32_TIP_APL_LOG_H_
#define MOZC_WIN32_TIP_APL_LOG_H_
#include <cstdio>
#include <cstdarg>
#include <windows.h>
namespace mozc::win32::tsf {
inline void AplLog(const char* fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  FILE* f = fopen("c:\\tmp\\mozc_apl_debug.log", "a");
  if (f) { fprintf(f, "%s\n", buf); fclose(f); }
  OutputDebugStringA(buf);
  OutputDebugStringA("\n");
}
}  // namespace mozc::win32::tsf
#endif
```

### Implementation order rationale

- W1 is proto changes (config only, NOT commands.proto) — testable via build
- W2 is a registration change — testable via install + Settings UI
- W3 is language bar menu — visible in system tray, no behavioral change
- W4 is config wiring — menu toggles persist, no key handling changes
- W5 is glyph production — behavioral change (key intercept + text insertion)
- W6 is installer verification — full install/uninstall cycle

W5 comes after W4 because the key intercept reads the cached config fields
(which shifting key is active) that W4 wires up. W6 is last because the
installer is a "full stack" smoke test.

---

### Step W1: Config proto (no behavioral change)

Add the `AplShiftingKeySet` message and field 122 to `config.proto`.
**Do NOT modify `commands.proto`.**

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

**Cross-reference**: Field 122 is new — nothing references it yet. No
cascading changes needed. Field 121 is unused. Field 120 is
`use_mode_indicator`. No conflict.

**Logging**: None (proto-only step).

**Test**: Build succeeds. No runtime change.

---

### Step W2: English registration (replace Japanese with APL)

Change the existing profile from Japanese to English and update the display
name. This is the minimal change set to make Mozc appear as an English input
method.

**Files changed**:

| File | Line | Change |
|------|------|--------|
| `src/win32/base/tsf_profile.cc` | 74 | `MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT)` → `MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT)` |
| `src/win32/tip/tip_resource.rc` | 148-149 | `"Mozc"` → `"Mozc APL"` (Japanese string table, Mozc build) |
| `src/win32/tip/tip_resource.rc` | 191-192 | `"Mozc"` → `"Mozc APL"` (English string table, Mozc build) |
| `src/win32/custom_action/custom_action.cc` | 328 | `"0x0411:"` → `"0x0409:"` in `EnableTipProfile` |
| `src/win32/base/imm_util.cc` | 66 | `"0x0411:"` → `"0x0409:"` in `SetDefault` |
| `src/win32/base/imm_util.cc` | 91 | `MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)` → `MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)` |

**Exhaustive cross-reference** of all `0x0411` / `LANG_JAPANESE` sites in
`src/win32/`:

| Site | File:Line | Impact |
|------|-----------|--------|
| `kTextServiceLanguage` | `tsf_profile.cc:74` | **Changed** |
| `TsfProfile::GetLangId()` | `tsf_profile.cc:94` | Returns constant — auto-updated |
| `"0x0411:"` in `EnableTipProfile` | `custom_action.cc:328` | **Changed** |
| `"0x0411:"` in `SetDefault` | `imm_util.cc:66` | **Changed** |
| `LANG_JAPANESE` in `ActivateProfile` | `imm_util.cc:91` | **Changed** |
| `RegisterProfiles`/`UnregisterProfile` | `tsf_registrar.cc:193,222` | Uses `GetLangId()` — auto-updated |
| `EnsureKanaLockUnlocked` | `tip_text_service.cc:233-239,509,731` | No-op on English — safe |
| `IsKanaLocked` | `keyboard.h:128, keyboard.cc:90` | Returns false on English — safe |
| `IsPressed(VK_KANA)` | `keyboard.cc:1324` | Never pressed on English — safe |
| `conversion_mode_util.cc` | all | Language-agnostic — safe |
| `LANGUAGE LANG_JAPANESE` | `tip_resource.rc:142` | RC directive — still compiles, cosmetic |

**Logging**: None (W2 is a registration change, no runtime code paths to log).

**Test**: Build, install. Windows Settings → Language → English → Keyboard
shows "Mozc APL". System tray shows "Mozc APL".

---

### Step W3: APL shifting key language bar menu (UI only)

Replace the Japanese input mode menu with the APL shifting key menu.
Introduce `AplLog()` utility.

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

**New file**: `src/win32/tip/apl_log.h` (see "Debug logging" above)

**Files changed**:

| File | Change |
|------|--------|
| `src/win32/tip/tip_lang_bar_callback.h` | Add `kAplShiftingKeyLeftCtrl`(50)–`kAplShiftingKeyCapsLock`(54) to `ItemId` enum |
| `src/win32/tip/tip_resource.h` | Add `IDS_APL_SHIFTING_KEY`(140)–`IDS_APL_SHIFTING_CAPS_LOCK`(145) |
| `src/win32/tip/tip_resource.rc` | Add string values in both Japanese and English string tables |
| `src/win32/tip/tip_lang_bar_menu.h` | Add public `ToggleItemCheckmark(UINT item_id)` to `TipLangBarMenuButton` |
| `src/win32/tip/tip_lang_bar_menu.cc` | Implement `ToggleItemCheckmark`: iterate menu data, XOR `TF_LBMENUF_CHECKED`, call `OnUpdate` |
| `src/win32/tip/tip_lang_bar.h` | Replace `input_button_menu_` with `apl_shifting_button_` (`TipLangBarMenuButton`). Keep `input_mode_button_for_win8_` for system tray icon. Add `ToggleAplShiftingItem(UINT)`. |
| `src/win32/tip/tip_lang_bar.cc` | Major rewrite of `InitLangBar()`, `UninitLangBar()`, `UpdateMenu()`, `IsInitialized()` (see below) |
| `src/win32/tip/tip_text_service.cc` | Replace Japanese mode cases in `OnMenuSelect` with APL cases. Remove `GetMozcMode()`. |
| `src/win32/tip/BUILD.bazel` | Add `apl_log` header dep |

**Detail: `tip_lang_bar.cc` changes**:

- Remove `GetItemId()` function (lines 111-129) — no longer needed
- Remove `input_button_menu_` creation block (lines 157-194)
- Modify `input_mode_button_for_win8_` block: replace Japanese menu items
  with APL shifting key items + separator + tool/help items (merged tray menu
  — required for system tray icon to appear)
- Add new `apl_shifting_button_` block using `TipLangBarMenuButton` with 5
  APL items (standalone language bar button)
- `UninitLangBar()`: replace `input_button_menu_` cleanup with
  `apl_shifting_button_`
- `UpdateMenu()`: stub for now (just enable/disable), will be enhanced in W4
- `IsInitialized()`: check `input_mode_button_for_win8_` (not removed)
- Add `ToggleAplShiftingItem()` delegating to
  `apl_shifting_button_->ToggleItemCheckmark()`

**Detail: `tip_text_service.cc` changes**:

- Remove `GetMozcMode()` (lines 192-211)
- In `OnMenuSelect` (line 903): replace cases for `kDirect`/`kHiragana`/
  `kFullKatakana`/`kHalfAlphanumeric`/`kFullAlphanumeric`/`kHalfKatakana`
  with cases for `kAplShiftingKeyLeftCtrl`–`kAplShiftingKeyCapsLock`.
  W3 handler: call `langbar_.ToggleAplShiftingItem(menu_id)`.

**Exhaustive cross-reference of all removed/changed symbols**:

| Symbol | Sites | Action |
|--------|-------|--------|
| `input_button_menu_` | `tip_lang_bar.h:82`, `tip_lang_bar.cc:157-194,332-334,361,363,370` | Replaced with `apl_shifting_button_` |
| `GetItemId()` | `tip_lang_bar.cc:111-129,360` | Removed |
| `GetMozcMode()` | `tip_text_service.cc:192-211,911` | Removed |
| `kDirect..kHalfKatakana` in `OnMenuSelect` | `tip_text_service.cc:905-913` | Replaced with APL cases |
| `SwitchInputModeAsync` call | `tip_text_service.cc:912` | Removed from these cases |

**Logging**:
```
AplLog("W3: InitLangBar - APL shifting button created");
AplLog("W3: InitLangBar - tray menu created with APL items");
AplLog("W3: OnMenuSelect item_id=%d", menu_id);
```

**Menu structure**:
```
APL Shifting Key ▸
  ☐ Left Ctrl
  ☐ Right Ctrl
  ☐ Left Alt
  ☐ Right Alt
  ☐ Caps Lock
```

**Test**: Install, switch to Mozc APL. Click system tray icon — menu with 5
APL items appears. Click to toggle checkmarks (UI-only). Check
`c:\tmp\mozc_apl_debug.log` for W3 entries.

---

### Step W4: Config wiring (shifting key persistence)

Wire the menu toggles to read/write `config.proto` field 122 via the Mozc
server, and populate `InputBehavior` from config at startup.

**Files changed**:

| File | Change |
|------|--------|
| `src/win32/base/input_state.h` (line 56) | Add 5 bool fields to `InputBehavior`: `apl_shifting_left_ctrl`, `right_ctrl`, `left_alt`, `right_alt`, `caps_lock` (all `= false`) |
| `src/win32/base/config_snapshot.h` (line 42) | Add same 5 fields to `Info` |
| `src/win32/base/config_snapshot.cc` | In `GetConfigSnapshotImpl()`: read `config->apl_shifting_key_set()`. In `Get()`: copy to `Info*`. In `Info::Info()`: init to false. |
| `src/win32/tip/tip_private_context.cc` (line 80) | In `EnsureInitialized()`: copy 5 snapshot fields → `InputBehavior` |
| `src/win32/tip/tip_text_service.cc` | Enhance `OnMenuSelect` APL cases: `GetFocusedPrivateContext()` → `GetClient()` → `GetConfig/SetConfig` to persist. Update `InputBehavior` directly. Then toggle checkmark. |
| `src/win32/tip/tip_lang_bar.cc` | `UpdateMenu()`: accepts `InputBehavior`, sets checkmarks from its fields |
| `src/win32/tip/tip_lang_bar.h` | Update `UpdateMenu` to accept `InputBehavior` or equivalent |

**Config read path** (startup):
```
TipPrivateContext::EnsureInitialized()
  → ConfigSnapshot::Get(&snapshot)
    → config.apl_shifting_key_set().left_ctrl() etc.
  → behavior->apl_shifting_left_ctrl = snapshot.apl_shifting_left_ctrl
  → ... (all 5 fields)
```

**Config write path** (menu toggle):
```
OnMenuSelect(kAplShiftingKeyRightCtrl)
  → GetFocusedPrivateContext() → GetClient()
  → client->GetConfig(&config)
  → config.mutable_apl_shifting_key_set()->set_right_ctrl(!current)
  → client->SetConfig(config)               // persist via server
  → mutable_input_behavior()->apl_shifting_right_ctrl = !current
  → langbar_.ToggleAplShiftingItem(menu_id)  // visual update
```

**Cross-reference of `InputBehavior` readers** (must be safe with new fields):

| Site | File:Line | Impact |
|------|-----------|--------|
| `OnTestKey` | `tip_keyevent_handler.cc:241` | Copies by value — new fields copied but ignored. Safe. |
| `OnKey` | `tip_keyevent_handler.cc:410` | Same. Safe. |
| `ImeProcessKey` | `keyevent_handler.cc` | Only reads existing fields. Safe. |
| `ImeToAsciiEx` | `keyevent_handler.cc` | Same. Safe. |
| `GetOpenAndMode` | `tip_keyevent_handler.cc:107` | Only reads `prefer_kana_input`. Safe. |

**Cross-reference of `UpdateMenu` callers**:
- `tip_text_service.cc` `UpdateLangbar()` (~line 977) — needs signature update

**Logging**:
```
AplLog("W4: EnsureInitialized left_ctrl=%d right_ctrl=%d left_alt=%d right_alt=%d caps=%d", ...);
AplLog("W4: OnMenuSelect item=%d toggled to %d", menu_id, new_val);
AplLog("W4: SetConfig succeeded");
```

**Test**: Toggle "Right Ctrl", close/reopen menu — persists. Restart IME —
still persists. Check log for W4 entries.

---

### Step W5: APL glyph production (behavioral change)

Intercept key events when a configured shifting key is held, look up the APL
glyph, and insert it directly — consuming the keystroke before the application
sees it.

**5a. Virtual key → character mapping** — New file
`src/win32/tip/win32_apl_key_handler.h`:

```cpp
// Returns the US QWERTY character for a Win32 virtual key code.
char Win32VKToChar(WPARAM vk);

// Returns the US QWERTY shifted character for a base character.
char ShiftedChar(char base);

// Checks if any configured APL shifting key is currently held down.
bool IsAplShiftingKeyHeld(const InputBehavior& behavior);

// Given a VK code and shift state, look up the APL glyph.
// Returns a UTF-16 wstring (empty if no mapping).
std::wstring GetAplGlyphForVK(WPARAM vk, bool shift_held,
                               const InputBehavior& behavior);
```

`Win32VKToChar`: VK_A–VK_Z → 'a'–'z', VK_0–VK_9 → '0'–'9',
VK_OEM_* → US QWERTY lookup table.

`IsAplShiftingKeyHeld`:
```cpp
bool IsAplShiftingKeyHeld(const InputBehavior& behavior) {
  if (behavior.apl_shifting_left_ctrl  && (GetKeyState(VK_LCONTROL) & 0x8000)) return true;
  if (behavior.apl_shifting_right_ctrl && (GetKeyState(VK_RCONTROL) & 0x8000)) return true;
  if (behavior.apl_shifting_left_alt   && (GetKeyState(VK_LMENU)   & 0x8000)) return true;
  if (behavior.apl_shifting_right_alt  && (GetKeyState(VK_RMENU)   & 0x8000)) return true;
  if (behavior.apl_shifting_caps_lock  && (GetKeyState(VK_CAPITAL)  & 0x8000)) return true;
  return false;
}
```

Caps Lock uses `& 0x8000` (key currently pressed), not `& 0x0001`
(toggle state / LED on). This is a while-held modifier.

**5b. Key intercept in `OnTestKeyDown` / `OnKeyDown`** — The intercept goes
at the top of `OnTestKey()` and `OnKey()` in `tip_keyevent_handler.cc`,
immediately after getting `private_context`, BEFORE all existing Mozc key
processing:

```
OnTestKey: (decides whether to eat the key)
  if is_key_down && shifting key held:
    glyph = GetAplGlyphForVK(vk, shift_held, behavior)
    if glyph found → *eaten = TRUE; return
  if vk == VK_CAPITAL && caps_lock configured → *eaten = TRUE; return
  ... normal Mozc path ...

OnKey: (actually processes the key)
  if is_key_down && shifting key held:
    glyph = GetAplGlyphForVK(vk, shift_held, behavior)
    if glyph found → insert via TSF; *eaten = TRUE; return
  if vk == VK_CAPITAL && caps_lock configured → *eaten = TRUE; return
  if !is_key_down && vk == VK_LMENU && left_alt configured → *eaten = TRUE; return
  ... normal Mozc path ...
```

**5c. Caps Lock suppression** — Eat `VK_CAPITAL` keydown/keyup when Caps
Lock is configured as shifting key. TSF delivers `VK_CAPITAL` before the
toggle takes effect; returning `eaten=TRUE` suppresses the toggle.

**5d. Left Alt bare-press suppression** — Eat `VK_LMENU` keyup when Left
Alt is configured, preventing Win32 menu bar activation.

**5e. Text insertion** — TSF text insertion via `ITfInsertAtSelection`.
Add `TipEditSession::InsertTextSync()` static method that requests a sync
edit session calling `InsertTextAtSelection(ec, TF_IAS_NOQUERY, ...)`.

APL glyphs from `apl_keymap.h` are UTF-8 `string_view`; convert to single
`wchar_t` for TSF (all APL glyphs are BMP, no surrogate pairs needed).

**New files**:

| File | Role |
|------|------|
| `src/win32/tip/win32_apl_key_handler.h` | VK → char, shifting key check, glyph lookup |
| `src/win32/tip/win32_apl_key_handler.cc` | Implementation |
| `src/session/apl_keymap.h` | APL glyph table (cherry-pick or recreate from macOS/Linux) |
| `src/session/apl_keymap.cc` | Implementation |

**Files modified**:

| File | Change |
|------|--------|
| `src/win32/tip/tip_keyevent_handler.cc` | APL intercept at top of `OnTestKey()` and `OnKey()` |
| `src/win32/tip/tip_edit_session.h` | Add `InsertTextSync()` static method |
| `src/win32/tip/tip_edit_session_impl.cc` | Implement sync text insertion |
| `src/session/BUILD.bazel` | Add `apl_keymap` library rule |
| `src/win32/tip/BUILD.bazel` | Add `win32_apl_key_handler` rule, dep on `//session:apl_keymap` |

**Cross-reference of key event handler sites**:

| Site | File:Line | Impact |
|------|-----------|--------|
| `OnTestKey` | `tip_keyevent_handler.cc:157` | **Modified**: APL intercept at top |
| `OnKey` | `tip_keyevent_handler.cc:296` | **Modified**: APL intercept at top |
| `OnTestKeyDown/Up/OnKeyDown/Up` | `tip_keyevent_handler.cc:474-497` | Delegate to above — no change |

**Logging**:
```
AplLog("W5: OnTestKey VK=0x%x shift=%d shifting_held=%d", vk, shift, held);
AplLog("W5: OnKey VK=0x%x -> base='%c' -> glyph=U+%04X", vk, base, glyph[0]);
AplLog("W5: InsertTextSync %d chars", text.size());
AplLog("W5: Caps Lock eaten");
AplLog("W5: Left Alt release eaten");
```

**Test**: Enable "Right Ctrl". Notepad: hold R-Ctrl + `a` → `⍺`. Release →
normal `a`. R-Ctrl + Shift + `a` → shifted glyph. Caps Lock: hold → glyphs,
no LED toggle. Check log for W5 entries.

---

### Step W6: Installer verification

Verify the WiX installer properly registers and enables the APL profile with
the English language ID and that glyph production works end-to-end.

**Files changed**: None beyond W2. The installer custom actions
(`RegisterTIP`, `EnableTipProfile`) call into `TsfRegistrar` and
`TsfProfile` which were already updated in W2.

**Logging** (add to existing files):
```
AplLog("W6: EnableTipProfile desc=%S", desc.c_str());  // in custom_action.cc
AplLog("W6: RegisterProfiles langid=0x%x", GetLangId());  // in tsf_registrar.cc
```

**Test**: Clean Windows VM → install MSI → "Mozc APL" in Settings → menu
works → toggle persists → glyphs work → clean uninstall.

---

File Change Summary
--------------------

| Step | Files Modified | New Files |
|------|---------------|-----------|
| W1 | `config.proto` | — |
| W2 | `tsf_profile.cc`, `tip_resource.rc`, `custom_action.cc`, `imm_util.cc` | — |
| W3 | `tip_lang_bar_callback.h`, `tip_resource.h`, `tip_resource.rc`, `tip_lang_bar.h`, `tip_lang_bar.cc`, `tip_lang_bar_menu.h`, `tip_lang_bar_menu.cc`, `tip_text_service.cc`, `BUILD.bazel` (tip) | `apl_log.h` |
| W4 | `input_state.h`, `config_snapshot.h`, `config_snapshot.cc`, `tip_private_context.cc`, `tip_text_service.cc`, `tip_lang_bar.cc`, `tip_lang_bar.h` | — |
| W5 | `tip_keyevent_handler.cc`, `tip_edit_session.h`, `tip_edit_session_impl.cc`, `BUILD.bazel` (session + tip) | `win32_apl_key_handler.h/cc`, `apl_keymap.h/cc` |
| W6 | (logging only) `custom_action.cc`, `tsf_registrar.cc` | — |

---

Open Questions (Require Experimental Testing)
----------------------------------------------

### Q1: Does TSF deliver VK_CAPITAL before the Caps Lock toggle?

**Hypothesis**: YES. TSF key events arrive synchronously before the key's
default action. Returning `eaten=TRUE` from `OnTestKeyDown` should prevent
the Caps Lock LED and state from toggling.

**Test** (W5): Handle `VK_CAPITAL` in `OnTestKeyDown`. Press Caps Lock —
verify the LED does not toggle and the key is consumed.

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

| File | Role | Step |
|------|------|------|
| `src/protocol/config.proto` | Shared config proto | W1 |
| `src/win32/base/tsf_profile.cc` | Profile language ID | W2 |
| `src/win32/tip/tip_resource.rc` | Display name + APL strings | W2, W3 |
| `src/win32/custom_action/custom_action.cc` | Installer actions | W2, W6 |
| `src/win32/base/imm_util.cc` | IMM profile registration | W2 |
| `src/win32/tip/tip_lang_bar_callback.h` | Menu item IDs | W3 |
| `src/win32/tip/tip_resource.h` | Resource string IDs | W3 |
| `src/win32/tip/tip_lang_bar.h` | Language bar manager | W3, W4 |
| `src/win32/tip/tip_lang_bar.cc` | Language bar init/update | W3, W4 |
| `src/win32/tip/tip_lang_bar_menu.h` | Menu button class | W3 |
| `src/win32/tip/tip_lang_bar_menu.cc` | Checkmark toggle | W3 |
| `src/win32/tip/tip_text_service.cc` | Main TIP class | W3, W4 |
| `src/win32/base/input_state.h` | Live config state | W4 |
| `src/win32/base/config_snapshot.h` | Startup config cache | W4 |
| `src/win32/base/config_snapshot.cc` | Startup cache population | W4 |
| `src/win32/tip/tip_private_context.cc` | Per-context init | W4 |
| `src/win32/tip/tip_keyevent_handler.cc` | Key event processing | W5 |
| `src/win32/tip/tip_edit_session.h` | Edit session interface | W5 |
| `src/win32/tip/tip_edit_session_impl.cc` | Text insertion | W5 |
| `src/win32/base/tsf_registrar.cc` | Profile registration | W6 (logging) |

### New files

| File | Role | Step |
|------|------|------|
| `src/win32/tip/apl_log.h` | Debug logging utility | W3 |
| `src/win32/tip/win32_apl_key_handler.h` | VK→char, shifting key check, glyph lookup | W5 |
| `src/win32/tip/win32_apl_key_handler.cc` | Implementation | W5 |
| `src/session/apl_keymap.h` | APL glyph table (shared) | W5 |
| `src/session/apl_keymap.cc` | Implementation | W5 |

---

Known Risks and Mitigations
=============================

**Risk**: Changing the language from Japanese to English may break assumptions
elsewhere in the TIP code (e.g., `EnsureKanaLockUnlocked()` in `ActivateEx`,
Japanese-specific keyboard handling in `TipKeyeventHandler`).

*Mitigation*: See the exhaustive audit table in W2 above. All Japanese-specific
sites either get updated by the language ID change or are genuine no-ops under
English. No category (c) sites requiring guards have been identified.

**Risk**: `ConfigSnapshot::Get()` is a `static const` one-shot cache — it
cannot reflect config changes made after startup.

*Mitigation*: The key event handler never reads `ConfigSnapshot` directly.
It reads `InputBehavior`, a mutable per-context struct owned by
`TipPrivateContext`. `ConfigSnapshot` is used only to populate
`InputBehavior` at startup (in `EnsureInitialized()`). After a menu toggle,
the handler updates `InputBehavior` directly — the new value is already
known (it was just toggled), so no IPC round-trip or cache refresh is
needed. `SetConfig` persists the change to disk for cross-restart durability.

**Risk**: The Mozc server may not be running when the APL profile is
activated (e.g., server crashed or was not started).

*Mitigation*: `GetConfig`/`SetConfig` failures are non-fatal. The menu
shows unchecked items as default. The broker auto-restarts the server.

**Risk**: Caps Lock is a toggle key on Windows — pressing it changes
persistent LED state.

*Mitigation*: The APL key intercept eats `VK_CAPITAL` in `OnTestKeyDown`
(returning `eaten=TRUE`) to suppress the toggle. Uses `& 0x8000` (key
currently pressed) for detection, not `& 0x0001` (toggle state). The
intercept runs before `TipKeyeventHandler` processes the key, preventing
the existing handler at `keyevent_handler.cc:460` from forwarding
`VK_CAPITAL` to the Mozc server as `KeyEvent::CAPS`.


