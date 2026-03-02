APL Input Mode — Adding APL as a First-Class Mozc Input Mode
=============================================================

This document describes the architecture for adding APL as a macOS input mode
alongside the existing Hiragana, Katakana, etc. modes. This replaces the
original M2 plan (a simple `apl_mode_active_` bool in the controller) with a
proper integration into Mozc's composition mode pipeline.

For the macOS-specific IMK context, see [apl_macos.md](apl_macos.md).

---

Motivation
----------

Mozc on macOS exposes 5 input modes via the standard macOS input source menu:
Hiragana, Katakana, Half-width Katakana, Full-width Alphanumeric, and
Alphanumeric. Rather than tracking APL as a side-channel flag, we add it as a
6th mode so it appears in the macOS keyboards menu alongside the others.

Benefits:
- Reuses the entire existing mode-switching infrastructure (keyboard shortcuts,
  status bar icon, `switchMode:client:`, comeback mode, etc.)
- The macOS input source menu "just works" — users see APL and can switch with
  the standard UI
- Per-language APL variants (English, Finnish, Danish, German) map naturally to
  additional `CompositionMode` values in the future
- No special-case `handleEvent:` intercept needed for mode switching — the
  Composer already decides how to transliterate based on mode

---

Architecture Overview
---------------------

### Existing mode pipeline

```
Info.plist (register mode with macOS)
    → GetCompositionMode()     (mode ID string → CompositionMode enum)
    → switchMode:client:       (sends SWITCH_COMPOSITION_MODE to server)
    → Session::ApplyCompositionMode()  (maps to TransliterationType)
    → Composer::SetInputMode() (selects Transliterator)
    → Transliterator::Transliterate()  (transforms keystrokes to output)
```

### What we add at each layer

| Layer | File | Change |
|-------|------|--------|
| Proto enum | `src/protocol/commands.proto` | `APL = 6` in `CompositionMode` |
| Transliteration type | `src/transliteration/transliteration.h` | `APL` in `TransliterationType` |
| Transliterator enum | `src/composer/transliterators.h` | `APL` in `Transliterators::Transliterator` |
| Transliterator impl | `src/composer/transliterators.cc` | `AplTransliterator` class (passthrough stub) |
| Composer dispatch | `src/composer/composer.cc` | `APL` case in `GetTransliterator()` |
| Session dispatch | `src/session/session.cc` | `ApplyCompositionMode`, `ToCompositionMode`, `CompositionModeApl` |
| Session header | `src/session/session.h` | `CompositionModeApl` declaration |
| Keymap enums | `src/session/keymap.h` | `COMPOSITION_MODE_APL` in 4 state enums |
| macOS mode IDs | `src/mac/mozc_imk_input_controller.mm` | `kAplModeId`, `GetCompositionMode`, `GetModeId` |
| macOS registration | `src/mac/Info.plist` | APL entry in `ComponentInputModeDict` |
| Localization | `src/mac/{English,Japanese}.lproj/InfoPlist.strings` | Display name |

---

Detailed Changes
----------------

### 1. `src/protocol/commands.proto`

Add `APL = 6` to `CompositionMode`, bump sentinel:

```protobuf
enum CompositionMode {
  DIRECT = 0;
  HIRAGANA = 1;
  FULL_KATAKANA = 2;
  HALF_ASCII = 3;
  FULL_ASCII = 4;
  HALF_KATAKANA = 5;
  APL = 6;
  NUM_OF_COMPOSITIONS = 7;
}
```

### 2. `src/transliteration/transliteration.h`

Add `APL` before `NUM_T13N_TYPES`:

```cpp
  HALF_KATAKANA,
  APL,
  NUM_T13N_TYPES
```

### 3. `src/composer/transliterators.h`

Add `APL` to the `Transliterator` enum before `LOCAL`:

```cpp
  HALF_ASCII,
  APL,
  LOCAL,
```

### 4. `src/composer/transliterators.cc`

New transliterator class — passthrough stub identical in behavior to
`HalfAsciiTransliterator` but without the full-width-to-half-width conversion.
Raw keystrokes pass through unchanged. M3 will replace the `Transliterate()`
body with APL character table lookup.

```cpp
class AplTransliterator : public internal::TransliteratorInterface {
 public:
  ~AplTransliterator() override = default;
  std::string Transliterate(absl::string_view raw,
                            absl::string_view converted) const override {
    return std::string(raw.empty() ? converted : raw);
  }
  bool Split(size_t position, absl::string_view raw,
             absl::string_view converted, std::string* raw_lhs,
             std::string* raw_rhs, std::string* converted_lhs,
             std::string* converted_rhs) const override {
    return Transliterators::SplitRaw(position, raw, converted, raw_lhs,
                                     raw_rhs, converted_lhs, converted_rhs);
  }
};
```

Add factory case in `Transliterators::GetTransliterator()`:

```cpp
case APL:
  return Singleton<AplTransliterator>::get();
```

### 5. `src/composer/composer.cc`

Add case in `GetTransliterator()` (line ~75):

```cpp
case transliteration::APL:
  return Transliterators::APL;
```

### 6. `src/session/session.cc`

Three changes:

**a) `ApplyCompositionMode()`** — add case:

```cpp
case commands::APL:
  SwitchInputMode(transliteration::APL, composer);
  break;
```

**b) `ToCompositionMode()`** — add reverse mapping:

```cpp
case transliteration::APL:
  mode = commands::APL;
  break;
```

**c) `SWITCH_COMPOSITION_MODE` dispatch** (~line 297) — add case:

```cpp
case commands::APL:
  result = CompositionModeApl(command);
  break;
```

**d) New method** `CompositionModeApl`:

```cpp
bool Session::CompositionModeApl(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  SwitchInputMode(transliteration::APL, context_->mutable_composer());
  OutputFromState(command);
  return true;
}
```

**e) Keymap dispatch blocks** — add `COMPOSITION_MODE_APL` case in each of the
4 state dispatch switches (DirectInputState, PrecompositionState,
CompositionState, ConversionState) alongside existing COMPOSITION_MODE_*
entries.

### 7. `src/session/session.h`

Add declaration (~line 210):

```cpp
bool CompositionModeApl(mozc::commands::Command* command);
```

### 8. `src/session/keymap.h`

Add `COMPOSITION_MODE_APL` to all 4 state enums:
- `DirectInputState::Commands`
- `PrecompositionState::Commands`
- `CompositionState::Commands`
- `ConversionState::Commands`

### 9. `src/mac/mozc_imk_input_controller.mm`

Add mode ID constant:

```cpp
constexpr absl::string_view kAplModeId =
    "com.apple.inputmethod.Japanese.Apl";
```

Add to `GetCompositionMode()`:

```cpp
if (mode_id == kAplModeId) {
  return mozc::commands::APL;
}
```

Add to `GetModeId()`:

```cpp
case mozc::commands::APL:
  return kAplModeId;
```

### 10. `src/mac/Info.plist`

Add APL entry in `tsInputModeListKey` dict and visible modes array.
Uses `direct.tiff` as placeholder icon. No keyboard shortcut assigned.
`TISIntendedLanguage` is `en`. `tsInputModeScriptKey` is `smRoman`.

### 11. Localization strings

**English** (`src/mac/English.lproj/InfoPlist.strings`):
```
com.apple.inputmethod.Japanese.Apl = "APL (${INPUT_MODE_ANNOTATION})";
```

**Japanese** (`src/mac/Japanese.lproj/InfoPlist.strings`):
```
com.apple.inputmethod.Japanese.Apl = "APL (${INPUT_MODE_ANNOTATION})";
```

---

Future Extensibility: APL Input Methods Within the APL Mode
------------------------------------------------------------

The single `CompositionMode::APL` / macOS input source supports multiple input
methods internally — the mode is the *what* (APL characters), while the input
method is the *how*:

- **Shifting key** (M5+): Modifier held → `handleEvent:` intercepts before
  composer; transliterator maps virtual key to APL char. Controlled by
  `AplShiftingKeySet` config from M1.
- **Prefix/dead-key** (future): A prefix keystroke (e.g., backtick) puts the
  composer into "next key is APL" state. The composer accumulates a 2-key
  sequence and the transliterator resolves it.
- **Keyword search** (future): Longer sequences (e.g., `iota`) resolved by the
  transliterator or via the candidate window.

All methods live under the same "APL" entry in the macOS keyboard menu. The
user selects their preferred method via config. Multiple methods can coexist
(e.g., shifting key for common glyphs + keyword fallback for obscure ones).
The `AplTransliterator` is the extensibility point — it reads config and
dispatches to the right strategy. No changes to mode registration or switching
machinery are needed when adding new input methods.

Per-language APL variants (English, Finnish, Danish, German) each get their own
`CompositionMode` and macOS input source in a future milestone, since they
represent different keyboard layouts (different key→glyph mappings), not
different input methods.

---

Stub Behavior (This Milestone)
-------------------------------

The `AplTransliterator` is a passthrough: raw keystrokes come out as ASCII
unchanged. This means:

- APL appears in the macOS input source menu and can be selected
- The mode indicator changes in the menu bar
- Typing produces regular ASCII characters (no APL glyphs yet)
- Switching back to Hiragana works normally

M3 will replace the transliterator body with the APL character table lookup.

---

Verification
------------

1. **Build**: `bazel build //src/mac:mozc` — must compile without errors
2. **Proto generation**: Verify `commands.pb.h` includes `APL = 6`
3. **Functional test on macOS**:
   - Install the built IME (full Mac restart may be needed)
   - Open System Settings → Keyboard → Input Sources
   - Verify "APL" appears in the list of available modes
   - Select APL mode — verify the mode indicator changes in the menu bar
   - Type characters — they should pass through as regular ASCII
   - Switch back to Hiragana — verify Japanese input still works
4. **Unit tests**: Run existing session and composer tests. The new enum values
   must be handled in all switch statements (compiler warnings for unhandled
   cases will catch misses).
