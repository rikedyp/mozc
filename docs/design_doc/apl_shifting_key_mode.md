APL Shifting-Key Input Mode
===========================

Objective
---------

Enable APL glyph input by designating one or more modifier keys as an "APL shifting key". While the shifting key is held, ordinary key presses produce APL glyphs instead of their Latin equivalents — mirroring the physical APL keyboard overlays used historically and in modern APL environments.

Background
----------

Traditional APL keyboards assign APL glyphs to the shifted or "APL-shifted" positions of standard keys. On a modern keyboard without a physical APL overlay, this is typically emulated by using a modifier key as a mode-shift: holding the modifier causes each key to emit the APL glyph assigned to that key position.

Different APL implementations use different shifting keys. We want to experiment with several candidates to find what works best on Linux/IBus and is least likely to conflict with existing system or application shortcuts.

Candidate Shifting Keys
-----------------------

| Modifier | Notes |
|----------|-------|
| Ctrl | Standard APL keyboard convention on many platforms; highest priority for initial experiments. May conflict with application shortcuts (Ctrl+C, Ctrl+V, etc.). |
| Alt / Meta | Common in terminal-based APL environments. Risk of conflict with application menu shortcuts. |
| AltGr (Right Alt) | Used by GNU APL and Dyalog APL on Linux. Less conflict with common shortcuts; promising candidate. |
| Super (Windows key) | Largely reserved by the desktop environment (GNOME, KDE) for system shortcuts; may be difficult to intercept reliably. |
| Caps Lock | Repurposing Caps Lock as a latching APL-shift key is an option; avoids hold-down ergonomic issues. Requires key remapping at a lower level. |

Goals
-----

1. Implement Ctrl as the initial APL shifting key, producing APL glyphs for the standard APL key assignments while Ctrl is held.
2. Evaluate feasibility of AltGr as an alternative or additional shifting key, as it is the most conflict-free candidate on Linux.
3. Assess whether Alt, Super, and Caps Lock can be supported within the IBus/Mozc architecture.
4. Ensure the shifting-key mode can be toggled or configured per user, rather than being hardcoded.

Open Questions
--------------

- Which keys in the IBus key event pipeline are accessible as modifiers vs. intercepted by the compositor or kernel?
- Can Caps Lock be intercepted at the IBus level, or does it require an XKB or udev rule?
- Should the shifting key be configurable at runtime (e.g. via an IBus preference panel), or is a compile-time default sufficient for now?
