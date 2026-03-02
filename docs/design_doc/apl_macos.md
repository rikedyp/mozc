APL Glyph Input — macOS / InputMethodKit Implementation
=========================================================

This document covers the macOS-specific implementation of APL shifting-key input.
For the project overview, architecture, and Linux notes see [apl_linux.md](apl_linux.md).

---

Overview
--------

macOS uses Apple's **InputMethodKit (IMK)** framework rather than IBus. The entry
point for key events is `handleEvent:client:` in
`src/mac/mozc_imk_input_controller.mm`. This is architecturally cleaner than the
Linux/IBus path in one important respect: the IMK event dispatch is
**synchronous** — the application does not receive a keystroke until the IME
returns from `handleEvent:`. Returning `YES` reliably suppresses the event.

This is the key difference from the Linux/Electron problem documented in
`ctrl_key_wip.md`. On macOS, Ctrl+A through the IME should be suppressible even
in Electron-based apps (VSCode, Ride). This needs experimental confirmation but
the mechanism is fundamentally sound.

---

Available Modifiers (macOS)
----------------------------

| Modifier | NSEventModifierFlag | Notes |
|----------|---------------------|-------|
| Ctrl | `NSEventModifierFlagControl` | Clean; no bare-press side-effects. Many terminal shortcuts use Ctrl, but IME can suppress them. |
| Option (Alt) | `NSEventModifierFlagOption` | Clean; no bare-press side-effects on macOS (unlike Linux Alt → menu-bar). Option+key normally produces Unicode chars (å, ©, etc.) — IME intercept prevents these from reaching app. |
| Caps Lock | `NSEventModifierFlagCapsLock` | Currently **stripped** in `KeyCodeMap.mm` line 160. Requires special handling in `handleEvent:` before `getMozcKeyCodeFromKeyEvent:`. |
| Command | `NSEventModifierFlagCommand` | **Blocked** — `KeyCodeMap.mm` returns `NO` when Command is held (line 191–195). Heavily grabbed by macOS system and apps. Not viable. |
| Fn | `NSEventModifierFlagFunction` | Available but unusual. Fn+key produces alternate keycodes (F1–F12, etc.) which disrupts glyph lookup. Not viable for shifting. |

**No Left/Right distinction**: Standard macOS APIs report only the combined
`NSEventModifierFlagOption` / `NSEventModifierFlagControl` — no L/R split. The
undocumented lower bits of `modifierFlags` *do* contain L/R information but are
fragile and unsupported. The macOS implementation will offer "Ctrl" and "Option"
as unified choices (not Left/Right), unlike the Linux implementation.

**Practical candidates**: Ctrl and Option. Caps Lock requires extra work but is
feasible. Command is not viable.

---

Physical Key Codes (macOS vs Linux)
------------------------------------

Linux uses evdev scancodes (`keycode` in IBus, e.g. 30 = QWERTY 'A'). macOS uses
**Carbon virtual key codes** (`[event keyCode]`, e.g. `kVK_ANSI_A = 0x00`). Both
are physical-position-based and layout-independent, serving the same purpose.

The Linux `AplKeycodeToChars` scancode table needs a macOS equivalent using
`kVK_ANSI_*` constants from `<Carbon/Carbon.h>`. The existing `apl_keymap.cc`
glyph table (keyed by ASCII char) is reusable; only the
`virtualKeyCode → ASCII char` mapping is new.

Key macOS virtual key codes for ANSI layout:

```
kVK_ANSI_A=0x00  kVK_ANSI_S=0x01  kVK_ANSI_D=0x02  kVK_ANSI_F=0x03
kVK_ANSI_H=0x04  kVK_ANSI_G=0x05  kVK_ANSI_Z=0x06  kVK_ANSI_X=0x07
kVK_ANSI_C=0x08  kVK_ANSI_V=0x09  kVK_ANSI_B=0x0B  kVK_ANSI_Q=0x0C
kVK_ANSI_W=0x0D  kVK_ANSI_E=0x0E  kVK_ANSI_R=0x0F  kVK_ANSI_Y=0x10
kVK_ANSI_T=0x11  kVK_ANSI_1=0x12  kVK_ANSI_2=0x13  kVK_ANSI_3=0x14
kVK_ANSI_4=0x15  kVK_ANSI_6=0x16  kVK_ANSI_5=0x17  kVK_ANSI_Equal=0x18
kVK_ANSI_9=0x19  kVK_ANSI_7=0x1A  kVK_ANSI_Minus=0x1B  kVK_ANSI_8=0x1C
kVK_ANSI_0=0x1D  kVK_ANSI_RightBracket=0x1E  kVK_ANSI_O=0x1F
kVK_ANSI_U=0x20  kVK_ANSI_LeftBracket=0x21  kVK_ANSI_I=0x22  kVK_ANSI_P=0x23
kVK_ANSI_L=0x25  kVK_ANSI_J=0x26  kVK_ANSI_Quote=0x27  kVK_ANSI_K=0x28
kVK_ANSI_Semicolon=0x29  kVK_ANSI_Backslash=0x2A  kVK_ANSI_Comma=0x2B
kVK_ANSI_Slash=0x2C  kVK_ANSI_N=0x2D  kVK_ANSI_M=0x2E  kVK_ANSI_Period=0x2F
kVK_ANSI_Grave=0x32
```

Note: these are *not* sequential and differ from both QWERTY order and Linux
evdev values. `generate_mapping.py` in `src/mac/` may be useful for generating
tables.

---

APL Mode on macOS
-----------------

### The mode registration problem

On macOS, input modes are declared in `Info.plist` under
`ComponentInputModeDict`. The current modes are all Japanese + Roman. `APL = 6`
from `commands.proto` is not registered here.

`GetCompositionMode()` in `mozc_imk_input_controller.mm` maps a macOS mode ID
string to a `CompositionMode`. `GetModeId()` maps in the other direction. Neither
handles `APL = 6` — the `default:` case in `GetModeId()` returns `kRomanModeId`.

**Options**:

1. **Register APL as a first-class mode in Info.plist**: Add an entry like
   `com.google.inputmethod.Japanese.APL`. Requires adding this ID to
   `GetCompositionMode()` / `GetModeId()` and modifying `Info.plist`. Cleanest
   long-term solution; allows the APL mode icon to appear in the menu bar.

2. **Track APL as a flag within Roman mode**: Keep `mode_ = HALF_ASCII` on the
   macOS side and maintain a separate `apl_mode_active_` bool in the controller
   (mirroring how `session.cc` tracks it server-side). APL mode would appear as
   "Roman" in the macOS menu bar — acceptable for a POC.

**Recommendation for POC**: Option 2 (flag within Roman mode). It avoids the
Info.plist registration complexity and the restriction that new mode IDs require
re-login after IME installation to appear in the menu bar.

### Mode switching on macOS

macOS calls `setValue:forTag:client:` when the user picks a mode from the menu
bar. This calls `GetCompositionMode()` → `switchMode:`. If APL mode needs to
survive focus changes, the `activateServer:` → `handleConfig` path needs to
restore it, similar to what was done for `MakeSureIMEOn()` on Linux.

---

Architecture: Intercept in handleEvent:
-----------------------------------------

The macOS equivalent of the Linux `mozc_engine.cc` intercept block:

```
handleEvent:client: (NSEvent *event)
  │
  ├── [FlagsChanged: track modifier held state if needed]
  │
  ├── [APL mode active AND shifting modifier held (check [event modifierFlags])
  │    AND [event keyCode] is in AplVirtualKeyToChars table]
  │     chars = AplVirtualKeyToChars([event keyCode])   ← layout-independent
  │     is_shifted = ([event modifierFlags] & NSEventModifierFlagShift)
  │     ch = is_shifted ? chars.shifted : chars.unshifted
  │     glyph = GetAplShiftedGlyph(ch) || GetAplGlyph(ch)
  │     if glyph: [sender insertText:glyph ...]; return YES
  │
  ├── [APL mode active AND no shifting modifier AND printable keycode]
  │     [sender insertText:([event characters]) ...]; return YES
  │     (autocomplete suppression — bypass Mozc server)
  │
  └── [everything else] → getMozcKeyCodeFromKeyEvent: → send to server
```

This is the clean architecture from `apl_linux.md` "Design Notes: Scancode-First
Architecture", adapted for macOS. All glyph output happens in the controller
before the Mozc server is involved.

**Why intercept before getMozcKeyCodeFromKeyEvent:**

- `getMozcKeyCodeFromKeyEvent` uses `[event characters]` / `[event
  charactersIgnoringModifiers]` — for Option+A this gives 'å' / 'a'. For the
  APL path we want to use `[event keyCode]` directly (physical key position).
- `getMozcKeyCodeFromKeyEvent` returns `NO` for Cmd-modified events (line 191).
  Though Cmd isn't a planned shifting key, the early intercept keeps the logic
  self-contained.

**Config access**: The `AplShiftingKeySet` config is read from the Mozc server
via `mozcClient_->GetConfig(&config)`, same as `handleConfig` does. We cache the
shifting key setting in the controller to avoid per-keystroke IPC.

---

Shifting Key UI (NSMenu)
-------------------------

Linux uses IBus panel properties (toggle buttons in the system tray). macOS uses
`NSMenu` already exposed via the `menu_` IBOutlet wired in `Config.xib`.

The menu already has items for Reconvert, Config Dialog, Dictionary Tool, etc.
A "APL Shifting Key" submenu with checkmark items (NSMenuItem with `state:
NSControlStateValueOn/Off`) is the natural macOS equivalent of the IBus panel
submenu.

Menu items would call an `IBAction` that reads/writes `Config.apl_shifting_key_set`
via `mozcClient_->GetConfig` / `mozcClient_->SetConfig`, then refreshes the
controller's cached shifting key state.

---

Open Questions (Require Experimental Testing)
----------------------------------------------

### Q1: Does IME event suppression work in VSCode on macOS?

**Hypothesis**: YES. On macOS, `handleEvent:client:` is synchronous and the
application does not receive the keystroke if it returns YES. Unlike Linux/Wayland
where the IBus D-Bus response can be async and Electron fires its accelerator
table before the response arrives, macOS IMK runs synchronously in the same event
dispatch loop.

**Test**: In the built IME with APL mode active and Ctrl as the shifting key,
press Ctrl+A in VSCode. Expected: ⍺ is inserted (not "select all"). If Ctrl+A
triggers "select all", the hypothesis is wrong.

**Alternative if Ctrl+A fails in VSCode**: Try Option. Option+letter is not in
VSCode's default accelerator table on macOS.

### Q2: Does IME event suppression work in Ride on macOS?

Same question as Q1, same test methodology. Ride uses a relatively old Electron
version.

**Additional concern**: Ride may restrict which input methods work at all —
some Electron apps disable IME input on certain text fields. Needs testing.

### Q3: Does VSCode take priority with its shortcuts when Command is held?

Cmd+key is not interceptable via IMK (Mozc framework filters it out). This is
expected and fine — Cmd+key should never be an APL shifting key.

Ctrl+key: answered by Q1.
Option+key: mostly not in VSCode's default bindings. Some (Option+Arrow for word
movement) may conflict. Needs testing.

### Q4: Can we distinguish Caps Lock reliably?

`NSEventModifierFlagCapsLock` is stripped in `getMozcKeyCodeFromKeyEvent:` (line
160 of `KeyCodeMap.mm`). If we intercept before that call (which we do in the
proposed architecture), we can check `[event modifierFlags] &
NSEventModifierFlagCapsLock` directly.

However: Caps Lock state toggles on press, so "held = typing APL" means the user
must toggle Caps Lock on to enter APL shifting mode and off to exit. This is a
UX design choice, not a technical blocker.

### Q5: Does bare Option press cause side effects in any apps?

On macOS, bare Option release does not activate menu bars (unlike Linux Alt). No
known issue. But some apps (particularly web-based) may have custom Option
handling. Needs spot testing.

---

Key Files (macOS)
-----------------

| File | Role |
|------|------|
| `src/mac/mozc_imk_input_controller.mm` | IMK frontend: `handleEvent:`, APL intercept, shifting key cache, NSMenu handler |
| `src/mac/mozc_imk_input_controller.h` | Controller members: `apl_mode_active_`, cached config |
| `src/mac/KeyCodeMap.mm` | Key event translation: `getMozcKeyCodeFromKeyEvent:`, modifier flag handling |
| `src/mac/Info.plist` | Mode declarations (may need APL entry; deferred per POC plan) |
| `src/mac/English.lproj/Config.xib` | NSMenu wiring for shifting key submenu |
| `src/session/session.cc` | `TryAplShiftedKey()`, `apl_mode_active_` (shared with Linux) |
| `src/session/apl_keymap.cc` | Glyph table (shared with Linux) |
| `src/protocol/config.proto` | `AplShiftingKeySet` (shared) |
| `src/protocol/commands.proto` | `CompositionMode::APL = 6` (shared) |

New file needed:

| File | Role |
|------|------|
| `src/mac/apl_keycode_map.h` or inline in controller | Table: `unsigned short kVK_* → char` for APL glyph lookup |

---

Differences from Linux Implementation
---------------------------------------

| Concern | Linux (IBus) | macOS (IMK) |
|---------|-------------|-------------|
| Event intercept point | `mozc_engine.cc` (IBus frontend) | `mozc_imk_input_controller.mm` `handleEvent:` |
| Physical key identifier | evdev scancode (`keycode` in IBus event) | Carbon virtual key code (`[event keyCode]`) |
| Modifier flags | `IBUS_CONTROL_MASK`, `IBUS_MOD1_MASK`, etc. | `NSEventModifierFlagControl`, `NSEventModifierFlagOption`, etc. |
| L/R modifier distinction | Possible (per-side held tracking) | Not reliably available in standard API |
| Cmd/Super | Not viable (DE grabs) | Not viable (Mozc framework filters out) |
| AltGr | Requires XKB symbols file | Not applicable (macOS has no AltGr; Option is the closest equivalent) |
| Electron/Wayland async issue | YES — documented bug with Ctrl+key in Ride | Expected NO — IMK is synchronous |
| Menu bar activation on bare Alt | Requires bare-Alt suppression in mozc_engine.cc | Not an issue on macOS |
| Shifting key UI | IBus panel toggle properties | NSMenu checkmark items in Config.xib |
| APL mode indicator | IBus language bar "⍺" property | Needs Info.plist entry OR remain as "Roman" for POC |
| XKB for AltGr | Yes, planned | Not needed |

---

Phase 1 Plan: macOS POC
========================

**Goal**: APL mode active; Option+A → ⍺ committed in at least one target app.

**Baseline**: Commit `d0a82cfa8e` — a reset Mozc build verified to support both
alphanumeric (latin) and hiragana typing on macOS. The existing menu bar submenu
shows: Reconversion, Preferences..., Add a word, Dictionary Tool..., About Mozc.

### Implementation order rationale

A previous attempt implemented all steps at once from the Linux shifting-key
commit (`e09e30b6ca`). Two problems were observed:

1. **Typing was completely broken** — no input worked at all. This *may* have
   been stale IME state on macOS (a full MacBook restart was needed before even
   the reset build worked), but the root cause was never isolated.
2. **The shifting key menu did not appear** — the NSMenu submenu for selecting
   Ctrl/Option was missing.

Because issue (1) is ambiguous and issue (2) is a real code problem, the safest
approach is to build incrementally from the verified-working reset commit. Each
step is independently testable: if typing breaks, the offending change is
immediately identifiable. The order below is chosen so that UI-only and
data-only changes come first (zero risk of breaking input), with the behavioral
`handleEvent:` intercept arriving last.

### Step M1: NSMenu shifting key submenu (UI only — no behavioral change) ✅ DONE

Add an "APL Shifting Key" submenu to the existing `menu_` with items "Ctrl",
"Option" (and optionally "Caps Lock"). Each item is checkmark-toggled via an
`IBAction` that writes to `Config.apl_shifting_key_set` via
`mozcClient_->GetConfig` / `mozcClient_->SetConfig`, then refreshes the
controller's cached shifting key state.

**Test**: Build and install. Open the Mozc menu from the macOS menu bar. Verify
the "APL Shifting Key" submenu appears with the expected items. Verify that
selecting an item persists across IME restarts. Verify that normal typing
(alphanumeric and hiragana) still works — this step must not affect input at all.

**Result**: Implemented and verified. Submenu appears with Ctrl and Option items;
checkmarks toggle and persist across IME restarts (Mac restart required after
install to clear stale IME state). Normal typing unaffected.

### Step M2: APL as a first-class input mode ✅ DONE

**Scope change**: M2 was originally a simple `apl_mode_active_` bool. It is now
a full input mode registration through the Mozc composition mode pipeline,
allowing APL to appear in the macOS keyboards menu alongside Hiragana, Katakana,
etc. See [apl_input_mode.md](apl_input_mode.md) for the detailed design.

**Test**: Build and install. Verify "APL" appears in the macOS input source
menu. Select it — type characters (they pass through as ASCII for now). Switch
back to Hiragana — verify Japanese input still works.

**Result**: Implemented and verified. Selecting APL (Mozc) from the macOS keyboard installer puts the option in the taskbar menu. Typing with APL mode active passes through latin characters. The autocomplete menu still appears, which we might want to remove or create an option depending on whether we are doing keyword-based input. This will be addressed at a later stage.

### Step M3: Virtual key → char table (pure data — no behavioral change)

Add `AplVirtualKeyToChar(unsigned short keyCode) → char` in the controller or a
small header. Maps `kVK_ANSI_A → 'a'`, `kVK_ANSI_B → 'b'`, etc., covering all
keys in the APL glyph table.

**Test**: Compile only. This is a data table with no call sites yet. Verify
the build succeeds and normal typing still works.

### Step M4: Config access for shifting key (wiring — no behavioral change)

Call `mozcClient_->GetConfig(&config)` in `handleConfig` and cache
`config.apl_shifting_key_set()` in an ivar. Refresh on every `activateServer:`.

**Test**: Build and install. Change the shifting key via the M1 submenu, then
switch apps and back. Verify (via `NSLog`) that the cached config matches what
was selected. Verify normal typing still works.

### Step M5: Intercept in handleEvent: (behavioral change — APL glyph insertion)

Before `getMozcKeyCodeFromKeyEvent:`, add the intercept block:

```objc
if (apl_mode_active_) {
  NSUInteger flags = [event modifierFlags];
  bool shiftHeld = (flags & NSEventModifierFlagShift) != 0;
  bool shiftingModHeld = /* check cached config: ctrl? option? */ false;
  if (flags & NSEventModifierFlagControl && config_uses_ctrl) shiftingModHeld = true;
  if (flags & NSEventModifierFlagOption && config_uses_option) shiftingModHeld = true;

  unsigned short vkCode = [event keyCode];
  char baseChar = AplVirtualKeyToChar(vkCode);
  if (baseChar != 0) {
    if (shiftingModHeld) {
      const char *glyph = shiftHeld ? mozc::GetAplShiftedGlyph(baseChar)
                                    : mozc::GetAplGlyph(baseChar);
      if (!glyph) glyph = mozc::GetAplGlyph(baseChar);
      if (glyph) {
        [[self client] insertText:[NSString stringWithUTF8String:glyph]
                replacementRange:replacementRange_];
        return YES;
      }
    } else if (!shiftingModHeld && !(flags & NSEventModifierFlagCommand)) {
      // Passthrough: commit the character directly (suppress autocomplete)
      [[self client] insertText:[event characters]
              replacementRange:replacementRange_];
      return YES;
    }
  }
}
```

**Test**: Build and install. Enable APL mode, select Option as the shifting key.
Press Option+A — expect ⍺. Press plain 'a' — expect 'a' committed directly.
Disable APL mode — expect normal Mozc behavior restored. If typing is broken,
this is the step that caused it.

### Step M6: Cross-app test matrix

1. Build and install on macOS.
2. Switch IME to Mozc, activate APL mode.
3. Test Ctrl+A and Option+A in: TextEdit, Terminal, VSCode, Ride.
4. Record which work, which don't, and what happens (glyph inserted / shortcut
   fires / nothing).

---

Known Risks and Mitigations
=============================

**Risk**: Electron apps (VSCode, Ride) fire keyboard shortcuts before IMK
response even on macOS.

*Mitigation*: If Ctrl fails, Option almost certainly works (Option+letter is not
in Electron's default macOS accelerator tables). The Option path is the primary
recommendation if Ctrl has issues.

**Risk**: `GetAplGlyph()` / `GetAplShiftedGlyph()` is currently in `session.cc`
context. Moving glyph lookup to the macOS controller requires either:
  (a) making `apl_keymap.h` a standalone header-only lookup (clean), or
  (b) including `session/apl_keymap.h` in the controller (acceptable).

*Mitigation*: `apl_keymap.h/.cc` are already separate files; include them in
the macOS controller directly.

**Risk**: macOS silently changes behaviour of `insertText:replacementRange:` in
newer versions (known instability in some IMK APIs).

*Mitigation*: Use `commitText:client:` wrapper already present in the controller
which calls `[sender insertText:...]` — this is the proven path for glyph output
on macOS.
