Mozc APL IME — Project Summary
===============================

This document summarises the Mozc codebase and the proposal to adapt it as a
cross-platform IME for Dyalog APL glyph input.

---

Mozc Overview
-------------

Mozc is an open-source Japanese Input Method Editor (IME) originally derived
from Google Japanese Input. It runs on Linux, macOS, and Windows.

### Architecture

Mozc is a **client/server system**:

- **Client** — a thin, platform-specific process that captures raw key events
  from the OS and forwards them to the server as `KeyEvent` protocol buffers.
  Platform integrations:
  - **Linux**: IBus
  - **macOS**: InputMethodKit (IMK)
  - **Windows**: Text Services Framework (TSF)

- **Server** — a platform-agnostic process that owns all input processing
  state. It receives raw key events and returns composed/converted text and
  candidate lists.

- **Renderer** — a separate process that draws the candidate window (the
  floating popup that appears during active input showing ranked conversion
  choices). It has three native implementations for correct cursor-relative
  positioning:
  - Linux: Qt
  - macOS: Cocoa
  - Windows: Win32

  The renderer communicates with the server via `renderer_command.proto`.

- **Config dialog** — a standalone Qt application for editing settings
  (keymaps, romanization tables, general preferences). Qt is used on all three
  desktop platforms, but the entry point is platform-specific: the IBus panel
  menu on Linux, the macOS input method menu, or the Windows language bar.

### Conversion Pipeline

The server processes input through several stages:

1. **Composer** — converts raw keyboard input into kana using Trie-based
   **preedit tables** (`.tsv` files in `src/data/preedit/`). For example,
   the romanji sequence `ka` becomes `か`. The tables define input/output/
   pending rules that handle multi-keystroke sequences.

2. **Converter** — transforms kana into kanji via **lattice-based
   morphological analysis**. Dictionary nodes are inserted into a lattice,
   connection costs between adjacent morphemes are evaluated, and a
   Viterbi-like algorithm finds the minimum-cost segmentation and word
   selection. This is not a simple dictionary lookup — it is a graph search
   over a weighted lattice.

3. **Predictor** — provides candidate suggestions from two sources:
   - **Dictionary predictor** — suggests words matching the current input
   - **User history predictor** — suggests words the user has previously
     selected

   The predictor operates in two modes:
   - **SUGGESTION** — fires automatically as the user types
   - **PREDICTION** — fires on explicit user action (e.g. pressing Tab)

4. **Rewriter** — post-processes conversion results (collocation correction,
   calculator expressions, symbol insertion, etc.).

### Key Mapping

Key-to-command mappings are defined in `.tsv` files under `src/data/keymap/`
(e.g. `ms-ime.tsv`, `kotoeri.tsv`, `atok.tsv`). The session layer maps
`(input state, key event)` pairs to commands. Input states include
DIRECT, PRECOMPOSITION, COMPOSITION, CONVERSION, and PREDICTION.

---

APL IME Proposal
----------------

The proposal is to adapt Mozc into a **cross-platform IME for Dyalog APL
glyph input** on Linux, macOS, and Windows. Rather than building an IME from
scratch, the project repurposes Mozc's mature multi-platform architecture.

Three proof-of-concept branches exist:
- `3-shifting-keys` — Linux/IBus
- `apl-shifting-macos` — macOS/IMK
- `windows-poc` — Windows/TSF

### Input Modes

Seven input modes are under consideration, organised into three layers:

**Glyph entry (how a single APL symbol is typed):**

1. **Shifting key** (while-held modifier) — hold a configurable modifier
   (Ctrl, Alt, Caps Lock, etc.) and press a letter to produce an APL glyph.
   Working prototypes exist on all three platforms. Users choose their
   preferred modifier(s) via a system-tray menu.

2. **Prefix / dead key** — press a trigger key (e.g. backtick) then a
   letter. The design favours reusing Mozc's composer table Trie for this.
   Not yet implemented.

3. **Toggle mode** — switch into an APL layer that stays active until
   toggled off; all keys produce APL glyphs without holding a modifier.
   Partially implemented on macOS (`CompositionMode::APL` wiring exists).

**Composition (building compound glyphs):**

4. **Overstrike** — type two compatible glyphs to compose a third
   (e.g. `○` + `|` → `⌽`). Limited to the preedit buffer since IMEs
   cannot reliably read application text.

5. **Tab completion** — type a glyph sequence then press a trigger key to
   compose (e.g. `=>` Tab → `→`). Requires resolving a Tab key conflict
   with the existing candidate selection keymap.

**Search / autocomplete:**

6. **Keyword search** — type an English word like "reverse" and get `⌽` as
   a candidate, using Mozc's pluggable predictor interface.

7. **Idiom autocomplete** — type APL glyph sequences and get longer
   expression completions (e.g. `+/` suggests `+/⍳`).

### Cross-Cutting Concerns

- **Modifier key availability**: Linux exposes left/right modifier
  distinction; macOS does not; Windows does. The shifting key menu adapts
  per platform.

- **Sync vs async key events**: macOS (IMK) and Windows (TSF) handle key
  events synchronously — the application does not see the keystroke until
  the IME returns. Linux (IBus) is asynchronous, which causes issues in
  Electron-based apps (e.g. VSCode, RIDE) where the app may process the
  key before the IME can suppress it.

- **Multi-locale keyboard layouts**: US, UK, Nordic, German, etc. need
  consideration for physical key positions.

- **Language registration**: The APL IME registers as an English (not
  Japanese) input method on each OS.

- **Japanese mode removal**: Japanese-specific input modes, menus, and
  dictionaries are stripped from the APL fork.
