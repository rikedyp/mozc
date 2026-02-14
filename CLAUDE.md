# Mozc APL Fork — Project Context

This is a fork of [Mozc](https://github.com/google/mozc), the open-source Japanese IME, being adapted to support **APL (A Programming Language) glyph input** on Linux/IBus.

## Goal

Allow users to type APL symbols (⍺ ⍵ ← → ⍳ ∘ ⌈ ⌊ ∊ ⍴ etc.) by typing key sequences through the IME, similar to how Mozc maps romaji to hiragana/kanji. The IME should convert typed key sequences into APL glyphs and present them as candidates.

## Architecture Overview

Mozc is a multi-platform IME with a client-server model:

```
User Input → Composer → Converter → Predictor → Rewriter → Candidates
              (transliteration)  (morphology)  (suggest)  (transform)
```

Key components for APL adaptation:

| Component | Path | Role |
|-----------|------|------|
| Composer / Table | `src/composer/` | Transliterates raw key sequences → strings. **Primary target** for APL key mappings. |
| Preedit tables | `src/data/preedit/` | Data files defining transliteration rules (romaji → kana). APL rules go here. |
| Session | `src/session/` | Manages IME state per user session |
| Engine | `src/engine/` | Orchestrates the full pipeline |
| Converter | `src/converter/` | Morphological analysis via lattice/Viterbi |
| Dictionary | `src/dictionary/` | Word lookup — may need APL symbol entries |
| Rewriter | `src/rewriter/` | Post-processing — could add an APL symbol rewriter |
| Unix/IBus | `src/unix/` | Linux platform integration |

## Build System

**Bazel** (primary). Key commands run from `src/`.

Python helper: `src/build_mozc.py`

Canonical build command (from `src/`):
```sh
bazelisk build package --config oss_linux --config release_build
```
Then install with `bash install.sh` (from `src/`).

Note: `--config release_build` sets `--compilation_mode=opt` **and** `-DABSL_MIN_LOG_LEVEL=100`, which compiles out ALL `LOG()` / `ABSL_LOG()` calls. For debug builds with logging, omit `--config release_build`.

> **IMPORTANT — Claude must never run the build.** Builds are slow. Always stop and ask the user to build, then wait for them to report the result before continuing.

## Key Files for APL Work

- `src/data/preedit/` — transliteration rule tables (study these for APL table format)
- `src/composer/table.cc` / `src/composer/table.h` — loads and applies transliteration rules
- `src/composer/composer.cc` — main composition logic
- `src/session/session.cc` — session state, mode switching
- `src/unix/ibus/` — IBus frontend (Linux)

## Language

Primary: C++ (modern, uses Abseil). Build/data: Python. IPC: Protocol Buffers.

## Current State

No APL modifications have been made yet. This is a clean Mozc checkout at the starting point of adaptation.

## Notes

- The preedit table format maps input key sequences to output strings — this is the most natural place to define APL glyph mappings (e.g., `\alpha` → `⍺`, `<-` → `←`)
- APL mode may need a dedicated input mode (like hiragana/katakana modes) switchable via a key
- The existing `single_kanji` and `symbol` subsystems in `src/data/` are worth studying as analogues
- The ability to switch to a "shifting key" input mode, e.g. outputting APL glyphs while Ctrl is held, is a requirement
- A keyword search functionality, e.g. search "reverse" and find ⌽, is a stretch goal
- A built-in idiom search like APLCart is a stretch goal
- A glyph composition (like TryAPL's Tab completion) mode and overstrike mode (e.g. ○ overstruck with | makes ⌽) are stretch goals
