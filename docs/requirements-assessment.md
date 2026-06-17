# Requirements Assessment & Prioritisation

This document is the work product of **PLAN.md step 1**. It breaks the
external-design requirements in [`requirements.md`](requirements.md) into
discrete, testable items; classifies each by priority (MoSCoW); tags each by how
it relates to the upstream **mozc** codebase (reuse / adapt / new-build); groups
them into milestones with a defined MVP scope; and records the open questions
and assumptions that need owner sign-off.

It is an analysis layer over `requirements.md`. `requirements.md` remains the
normative spec and the source of truth for tier assignments (the project owner's
decision). Where this document and the spec disagree, the spec wins and the
disagreement is logged under [Open questions](#open-questions).

## How to read the tables

- **ID** — the stable requirement ID from `requirements.md`.
- **Tier** — the spec's proposed tier: Essential / Nice-to-have / Stretch.
- **MoSCoW** — derived priority for planning:
  - Essential → **Must** (v1 cannot ship without it)
  - Nice-to-have → **Should** (v1 ships with it if feasible)
  - Stretch → **Could** (deferred; explicitly out of MVP)
- **mozc tag** — relationship to the upstream codebase:
  - **reuse-mozc** — existing mozc capability satisfies it largely as-is (re-skin / re-test only).
  - **adapt-mozc** — an existing mozc component is the right vehicle but needs APL-specific changes.
  - **new-build** — no meaningful mozc precedent; build it (may sit on reusable infra).
- **Likely component(s)** — where in the tree the work most plausibly lands, to seed PLAN.md section 2.

## Requirement-by-requirement assessment

### Installation, presentation, and platform support

| ID | Tier | MoSCoW | mozc tag | Likely component(s) | Testable acceptance criterion |
|----|------|--------|----------|---------------------|-------------------------------|
| R1 | Essential | Must | adapt-mozc | branding, installers (`src/win32/installer`, `src/mac/installer`, `src/unix`) | One product name + repo + docs set; no per-platform product identity differs except documented glyph/modifier conventions. |
| R2 | Essential | Must | adapt-mozc | platform integration (`src/win32/tip`, `src/mac`, `src/unix/ibus`) | Fresh install on each named platform yields working APL glyph entry in a standard app. |
| R3 | Nice-to-have | Should | reuse-mozc | `src/unix` (ibus/fcitx5) | On an out-of-set Linux env, documented steps produce glyph entry; failure here is not a v1 defect. |
| R4 | Essential | Must | new-build | `src/config`, default config / `src/data` | After default install with zero settings changes, a documented base gesture produces an APL glyph. |

### Switching behaviour

| ID | Tier | MoSCoW | mozc tag | Likely component(s) | Testable acceptance criterion |
|----|------|--------|----------|---------------------|-------------------------------|
| R5 | Essential | Must | adapt-mozc | key handling (`src/session`, platform key event paths) | Holding the configured switching key + a base key emits the mapped glyph; releasing it restores normal input. |
| R6 | Essential | Must | adapt-mozc | `src/composer` (`table.*`, `composition.*`) | Pressing the prefix key once then a base key emits the mapped glyph, with no modifier held. |
| R7 | Stretch | Could | adapt-mozc | `src/session` mode state, `src/config` | Toggling the switch key flips a persistent APL mode; multiple glyphs type without re-prefixing. |
| R8 | Essential | Must | new-build | `src/config`, `src/protocol/config.proto` | The active switching model is selectable in config and takes effect without rebuild. |
| R9 | Essential | Must | adapt-mozc | keymap config (`src/config`, platform key tables) | For the active model, the switching key can be set to any member of the documented per-platform set. |
| R10 | Nice-to-have | Should | new-build | `src/gui` settings dialog | Attempting to bind a system-reserved key triggers an explicit confirm prompt before applying (where the platform allows detection). |
| R11 | Essential (Win) | Must | new-build | `src/win32/tip`, glyph-map data | On Windows, default switching + mappings match the current official Dyalog Windows IME for its supported international locales. |
| R12 | Essential | Must | adapt-mozc | key passthrough, layout handling | With APL input active, non-APL keys still produce the correct national-layout characters for the listed layouts. |
| R13 | Stretch | Could | adapt-mozc + new-build | `src/composer/composition.*`, renderer | Overstrike / ASCII combos compose to the correct Unicode glyph; intermediate state is visible; order-sensitive combos (`\|-` vs `-\|`) resolve correctly. |

### Learning and discovery

| ID | Tier | MoSCoW | mozc tag | Likely component(s) | Testable acceptance criterion |
|----|------|--------|----------|---------------------|-------------------------------|
| R14 | Nice-to-have | Should | new-build (seed from aplcart.info / existing IME search) | search UI + APL keyword data | Typing a keyword form (e.g. `:reduce`) in any IME-active app offers/inserts the mapped glyph (`/`). |
| R15 | Stretch | Could | new-build | `src/renderer`, `src/gui` | A shortcut/menu summons a popup showing current mappings; available in any IME-active app. |
| R16 | Nice-to-have | Should | new-build | `src/gui`, docs | Every glyph's entry shortcut is discoverable via the UI and docs. |
| R17 | Stretch | Could | new-build (seed from aplcart.info / existing IME search) | search UI + idiom data | A documented mechanism lets the user discover and insert common idioms without retyping them. |

### System integration and consent

| ID | Tier | MoSCoW | mozc tag | Likely component(s) | Testable acceptance criterion |
|----|------|--------|----------|---------------------|-------------------------------|
| R18 | Essential | Must | reuse-mozc | platform integration (`src/win32/tip`, `src/mac`, `src/unix/ibus`) | One running instance provides APL input to IDE, Ride, TTY-IDE terminal, browser, editor — no per-app setup. |
| R19 | Essential | Must | reuse-mozc + new-build | installers, `src/gui` | Install/run makes no keyboard-config change without an explicit, consequence-stating confirmation. |
| R20 | Essential | Must | reuse-mozc | platform registration | The IME appears in the OS input-method picker on each platform. |
| R21 | Essential | Must | reuse-mozc | platform integration | Enabling/disabling per app is driven entirely by OS input-method controls. |
| R22 | Essential | Must | reuse-mozc | platform integration | Installing alongside Mozc/Pinyin/Hangul leaves all of them working; switching uses standard OS controls. |
| R23 | Essential | Must | new-build | docs | Docs name the specific switch/enable/disable controls per platform. |
| R24 | Essential | Must | new-build | docs | Docs show how to revert any keyboard-config change the IME makes. |

### Remote sessions

| ID | Tier | MoSCoW | mozc tag | Likely component(s) | Testable acceptance criterion |
|----|------|--------|----------|---------------------|-------------------------------|
| R25 | Essential | Must | reuse-mozc | platform integration | APL glyphs type correctly in PuTTY/Windows Terminal, Terminal.app/iTerm2, GNOME Terminal/Konsole/xterm. |
| R26 | Nice-to-have | Should | new-build | key handling | The IME can emit the enumerated special codes (e.g. Ctrl+Enter, Ctrl+Shift+Backspace). |
| R27 | Essential | Must | reuse-mozc | (inherent — local input) | A supported remote Unix TTY-IDE host needs no IME install for R25 to hold. |
| R28 | Stretch | Could | new-build | key handling | Against a remote Dyalog Classic host, the IME sends key codes that keep existing remote APL input working. |

### Localisation

| ID | Tier | MoSCoW | mozc tag | Likely component(s) | Testable acceptance criterion |
|----|------|--------|----------|---------------------|-------------------------------|
| R29 | Essential | Must | adapt-mozc | UI strings (`src/gui`), installers | All listed UI surfaces ship in English in v1. |
| R30 | Essential | Must | reuse-mozc | message-resource infra | All user-facing strings live in a documented, translator-editable form. |
| R31 | Nice-to-have | Should | reuse-mozc | resource fallback | With no translation for the locale, UI falls back to English and the fallback is visibly indicated. (Trivially met while only English ships.) |
| R32 | Nice-to-have | Should | reuse-mozc | message-resource infra | UI is available in the languages Dyalog ships, chrome + popup labels. |

### Lifecycle

| ID | Tier | MoSCoW | mozc tag | Likely component(s) | Testable acceptance criterion |
|----|------|--------|----------|---------------------|-------------------------------|
| R33 | Essential | Must | reuse-mozc | OS controls / IME off | The user can disable APL input without uninstalling and re-enable later. |
| R34 | Essential | Must | adapt-mozc | uninstallers (`src/*/installer`, `src/mac/Uninstaller`) | The IME uninstalls completely on each platform. |
| R35 | Essential | Must | new-build | docs | Docs explain how to find/change the active input method per platform. |

### Updates

| ID | Tier | MoSCoW | mozc tag | Likely component(s) | Testable acceptance criterion |
|----|------|--------|----------|---------------------|-------------------------------|
| R36 | Nice-to-have | Should | adapt-mozc | config storage, installer upgrade | After an update, switching model/key/mappings/customisations are preserved. |
| R37 | Nice-to-have | Should | new-build | `src/config`, first-run UX | A newly-introduced setting takes a documented default and is announced on first launch post-update. |
| R38 | Essential | Must | new-build | release process, release notes | A default-behaviour change is in the release notes and surfaced at update time. |

## Priority summary

- **Must (Essential — MVP):** R1, R2, R4, R5, R6, R8, R9, R11, R12, R18, R19, R20, R21, R22, R23, R24, R25, R27, R29, R30, R33, R34, R35, R38 — **24 items.**
- **Should (Nice-to-have):** R3, R10, R14, R16, R26, R31, R32, R36, R37 — **9 items.**
- **Could (Stretch):** R7, R13, R15, R17, R28 — **5 items.**

## mozc-reuse summary

- **reuse-mozc:** R3, R18, R20, R21, R22, R25, R27, R30, R31, R32, R33 — platform-integration and message-resource strengths inherited from mozc.
- **adapt-mozc:** R1, R2, R5, R6, R7, R9, R12, R29, R34, R36 — existing components are the right vehicle, retargeted from Japanese to APL.
- **new-build:** R4, R8, R10, R11, R14, R15, R16, R17, R23, R24, R26, R28, R35, R37, R38 — APL-specific behaviour, config schema, discovery UI, docs, and release process.

(R13 and R19 straddle two tags; see their rows.)

## Milestones & MVP scope

The **MVP is the full Must set** (the 24 Essential requirements). Milestones
order the work so platform-integration reuse lands first and Dyalog-specific
behaviour layers on top.

- **M0 — Foundations.** Single-product branding and the platform-integration
  baseline inherited from mozc: R1, R18, R20, R21, R22. *(Depends on PLAN.md
  §2–3 build decisions.)*
- **M1 — Core glyph input.** The defining feature set: R4, R5, R6, R8, R9, R12.
  This is the milestone that makes the product an APL IME.
- **M2 — Windows parity.** R11 (and the Windows side of R12). Replicating the
  official Dyalog Windows IME defaults for international locales.
- **M3 — Platform breadth & remote.** R2 across all named platforms, R25, R27.
- **M4 — Lifecycle, consent & docs.** R19, R23, R24, R33, R34, R35, R38.
- **M5 — Localisation infra.** R29, R30 (English UI + translator-editable strings).
- **M6 — Discovery & polish (post-MVP Should).** R3, R10, R14, R16, R26, R31, R32, R36, R37.
- **M7 — Stretch.** R7, R13, R15, R17, R28.

MVP = M0–M5. M6 ships where feasible; M7 is explicitly deferred.

## Resolved decisions

Owner sign-off received (2026-06-17). Decisions folded into `requirements.md` and
the tables above.

1. **Supported-platform list — resolved.** Ubuntu X11 (XFCE) is in the v1
   Essential set. macOS is **ARM Essential**; macOS (Intel) is **Stretch**.
   Recorded in the `requirements.md` conventions section.
2. **Linux framework — resolved.** **ibus is Essential (v1 target); fcitx5 is
   Stretch.** `src/unix` effort focuses on the ibus path for MVP. Recorded in the
   conventions section.
3. **Divergence from upstream — resolved.** Divergence is **unconstrained for the
   whole project.** This is an APL IME unrelated to Japanese input; upstream mozc
   is a jumping-off point, not a baseline to stay compatible with. There is no
   obligation to track upstream or limit changes (informs PLAN.md §2's
   upstream-tracking decision and removes the Wayland-divergence concern).
4. **R11 reference spec — resolved (source identified).** The authoritative
   mappings/behaviour will come from config files — likely existing `.din` files,
   or the developer will obtain them from the existing official Dyalog IME
   project. To be pulled in during M2.
5. **R26 special-codes list — deferred (still open).** The full IDE shortcut-code
   list is deferred until after the adapt-vs-rewrite assessment (PLAN.md §2). R26
   stays Should; its acceptance criterion is not fully testable until the list is
   enumerated.
6. **R14 / R17 build-vs-reuse — resolved (lean new-build).** Likely **new-build**;
   optionally seeded from [aplcart.info](https://aplcart.info) tables or the
   existing IME's search data, rather than mozc's `prediction`/`dictionary` infra.
   Tags above keep these as new-build; final call still belongs to §2.
7. **APLCart activation gesture — confirmed.** "Any valid shortcut combination can
   be configured to trigger it (Stretch)" is the intended meaning.
8. **R7 mode-toggle — confirmed deferred.** Stays Stretch / M7; not pulled into MVP
   despite mozc's existing on/off mode state.

## Assumptions

Holding unless the owner says otherwise.

- "Standard text input framework" coverage is sufficient for R18/R25; apps that
  bypass it (per the spec) are out of scope and not counted as v1 defects.
- MoSCoW maps directly from tiers (Essential→Must, Nice-to-have→Should,
  Stretch→Could); the owner may override individual tiers in `requirements.md`,
  and this document follows.
- mozc-tags are **preliminary** — they seed PLAN.md §2 (refactor vs. rewrite) but
  the definitive keep/adapt/discard/rewrite decision is made there with deeper
  component inventory.
- Upstream mozc is a **jumping-off point only**; there is no obligation to track it
  or to limit divergence (resolved decision #3). The reuse-mozc tags mean "the
  fastest path reuses this code," not "we must stay compatible with upstream."
- R31 is treated as trivially satisfied in v1 (English-only) and becomes
  meaningful only once R32 languages arrive.
- "v1" = the MVP scope defined above (M0–M5); Nice-to-have items ship in v1 only
  where feasible without risking the Must set.
