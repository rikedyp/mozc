External-design requirements for the Dyalog APL IME, derived from the approved user stories. Each requirement has a stable ID and a proposed tier. Final tier assignments are the project owner’s decision.

**Tiers:** Essential (v1 must ship with this), Nice-to-have (v1 should ship with this if feasible), Stretch (deferred; may not appear in v1).

**Conventions:**

- Requirements are platform-agnostic unless a platform is named.
- “Supported platforms” means Microsoft Windows, macOS (ARM), Ubuntu GNOME Wayland, Fedora GNOME Wayland, and Raspberry Pi (recent Raspbian). Other Linux desktop environments are explicitly Nice-to-have where they appear.
- “Standard text input framework” means the input method framework the host OS provides for third-party IMEs (TSF on Windows, the macOS input method system, ibus/fcitx5-style frameworks on Linux). Apps that bypass this framework are out of scope by construction.
- Requirements describe user-visible behaviour only. Internal robustness, reliability, and implementation concerns are out of scope at this stage.

### Installation, presentation, and platform support

**R1. Single cross-platform product.** _(Essential)_ The IME is presented to users as a single cross-platform product with one name, one repository, and one set of documentation covering all supported platforms. Glyph mappings and modifier conventions may differ between platforms where the platform requires it.

**R2. Works on all supported platforms in v1.** _(Essential)_ The IME installs and provides APL glyph input on Microsoft Windows, macOS (ARM), Ubuntu X11 (XFCE), Ubuntu GNOME Wayland, Fedora GNOME Wayland, and recent Raspbian on Raspberry Pi.

**R3. Best-effort support for other Linux environments.** _(Nice-to-have)_ On Linux desktop environments, window systems, and distributions outside the supported set, the IME works on a best-effort basis with documented setup steps. Failure on an unsupported environment is not a v1 defect.

**R4. Minimal configuration to start.** _(Essential)_ A new user can start entering APL glyphs with minimal configuration beyond the standard install. Defaults are chosen so that no user intervention is required for basic glyph entry.

### Switching behaviour

**R5. Held-modifier switching (“shifting key”) model.** _(Essential)_ The user can enter APL glyphs by holding a designated switching key (e.g. AltGr-style) while pressing a base key.

**R6. Prefix-key switching model.** _(Essential)_ The user can enter APL glyphs by pressing a designated prefix key once before each base key, without holding any modifier.

**R7. Mode-toggle switching model.** _(Stretch)_ The user can toggle APL input on and off with a designated switching key and enter sequences of APL glyphs without repeated prefixes or held modifiers.

**R8. Switching models are configurable.** _(Essential)_ The active switching model is chosen by configuration.

**R9. Switching key selectable from a defined set.** _(Essential)_ For whichever switching model is active, the user can choose the key that drives switching from a defined set of supported keys, so that conflicts with other applications’ shortcuts can be avoided. The specific set of supported keys is part of the design and is documented.

- Shifting keys:
  - Windows: LCtrl, LAlt, RCtrl, RAlt _(Essential)_. Caps, LShift, RShift _(Stretch)_
  - Linux: LCtrl, LAlt, RCtrl, RAlt _(Essential)_. Caps, LShift, RShift, LWin, RWin _(Stretch)_
  - macOS: option (alt), cmd _(Essential)_
- Prefix keys: Any key that can produce output can be configured as prefix key on any platform _(Essential)_
- Activate keyword search / APLCart: _(Stretch)_ any valid shortcut combination for the

**R10. Consent required before binding to system-reserved keys.** _(Nice-to-have)_ If the user attempts to bind the switching key to a key reserved by the host OS or desktop environment (Alt, Super, Cmd, Win, etc.), the IME prompts for explicit confirmation before applying the binding if possible.

**R11. Windows Ctrl-based configuration preserved.** _(Essential, Windows-only)_ On Windows, the existing official Dyalog IME’s Ctrl-based switching behaviour and glyph mappings are preserved as the default. This applies in particular to the international locales the current official Dyalog Windows IME supports.

**R12. National keyboard layouts remain usable.** _(Essential)_ When APL input is active, the user’s national keyboard layout remains usable for non-APL characters. The set of national layouts supported matches the set covered by the current official Dyalog Windows IME; other layouts work on a best-effort basis.

- Windows: British English, American English _(Essential)_. Belgian, Danish, Finnish, French, German, Italian, Spanish, Swedish _(Nice-to-have)_.
- non-Windows: British English, American English _(Essential)_.

**R13. Character composition.** _(Stretch)_ The user can enter APL glyphs by overstriking their component characters (overstrike) or pre-defined ASCII combinations (e.g. -> gives →). The intermediate composition is visible as it happens, and the result is the correct composed Unicode glyph. Available on all supported platforms. Be aware some combinations care about ordering (|- ⊢ vs. -| ⊣).

### Learning and discovery

**R14. Keyword-based glyph search.** _(Nice-to-have)_ The user can find and enter a glyph by typing a keyword form (e.g. `:reduce` to produce `/`). The keyword search is a feature of the IME itself, available in any application the IME is active in.

**R15. Popup keyboard layout map.** _(Stretch)_ The user can summon a popup showing the current APL glyph mappings, triggered by a keyboard shortcut or menu item. The popup is available in any application the IME is active in.

**R16. Discoverable shortcuts.** _(Nice-to-have)_ The keyboard shortcuts for entering glyphs are discoverable through the IME’s UI and documentation, so that users can progress from keyword search to fluent typing.

**R17. Access to common APL idioms.** _(Stretch)_ The IME provides a mechanism for the user to discover and insert common APL idioms without having to memorise or reconstruct them. The specific mechanism (autocomplete, browser, palette, snippet expansion, or similar) is left to detailed design.

### System integration and consent

**R18. System-wide input across applications.** _(Essential)_ A single instance of the IME provides APL input to any application that uses the host OS’s standard text input framework — including the Dyalog IDE, Ride, a terminal emulator running the TTY IDE, browsers, and editors. The user does not configure APL input separately per application.

**R19. No silent changes to keyboard configuration.** _(Essential)_ The IME does not change the user’s keyboard configuration without explicit consent. Where a change is required, the user is informed of the consequences and asked to confirm before it is applied.

**R20. OS input-method picker integration.** _(Essential)_ On each supported platform, the IME appears in the standard OS input-method picker (Windows language bar, macOS input menu, the GNOME/KDE/ibus/fcitx5 equivalents on Linux) so that users can find and select it through the mechanism they already know.

**R21. Per-application enable/disable via OS controls.** _(Essential)_ APL input is enabled or disabled per application according to the host OS’s input-method controls. This is satisfied by integrating with the OS framework rather than by IME-specific per-application configuration.

**R22. Coexists with other IMEs.** _(Essential)_ The IME installs alongside other input methods (Mozc, Pinyin, Hangul, etc.) without interfering with them. The user switches between APL and other writing systems using the same OS controls they already use.

**R23. Documented OS input-method controls.** _(Essential)_ For each supported platform, the documentation names the specific controls for switching, enabling, and disabling input methods — Windows language bar and Win+Space, macOS input menu and Ctrl+Space, the GNOME/KDE/ibus/fcitx5 equivalents on Linux. Note that these can be configurable within the operating system itself.

**R24. Revert instructions.** _(Essential)_ The user is shown how to revert any keyboard configuration changes the IME has made, so that the prior setup can be restored.

### Remote sessions

**R25. Common terminal emulator coverage.** _(Essential)_ R25 works in the common terminal emulators on each supported platform: PuTTY and Windows Terminal on Windows; Terminal.app and iTerm2 on macOS; GNOME Terminal, Konsole, and xterm on Linux.

**R26. IDE keyboard shortcuts.** _(Nice-to-have)_ The IME can produce special codes for keyboard shortcuts such as Ctrl+Enter and Ctrl+Shift+Backspace (full list to be enumerated in implementation plan in project repository).

**R27. Remote Unix hosts require no IME install.** _(Essential)_ Any Unix host Dyalog supports for the TTY IDE (AIX, Linux, etc.) requires no IME installation on the remote side for R25 to work.

**R28. Underscored alphabet.** _(Stretch)_ When the remote host runs Dyalog Classic Edition, the local IME sends appropriate key codes over the session so that existing remote APL input configuration continues to work.

### Localisation

**R29. English UI in v1.** _(Essential)_ The IME’s user interface — installer prompts, settings dialogs, keyboard popup, glyph search, error messages, tray/menu-bar labels — ships in English in v1.

**R30. Translatable UI strings are editable.** _(Essential)_ The IME’s user-facing strings are held in a documented form that translators can locate and edit.

**R31. Documented fallback language.** _(Nice-to-have)_ When the user’s system locale has no translation available, the IME falls back to English, and the fallback is visibly indicated so the user knows why the UI is not in their language. _(Trivially satisfied in v1 while only English ships; relevant once additional languages arrive.)_

**R32. Additional UI languages.** _(Nice-to-have)_ The IME’s UI is available in the languages Dyalog itself ships localised, including both menu chrome and keyboard popup labels. Not required for v1; enabled by R30.

### Lifecycle

**R33. Temporary disable.** _(Essential)_ The user can turn the APL IME off temporarily without uninstalling it, and re-enable it later.

**R34. Clean uninstall.** _(Essential)_ The user can uninstall the APL IME entirely.

**R35. Documented active-input-method controls.** _(Essential)_ The documentation explains how to find and change the currently-active input method on each supported platform.

### Updates

**R36. Configuration preserved across IME updates.** _(Nice-to-have)_ When the IME itself is updated, the user’s chosen switching model, switching key, glyph mappings, and other customisations are preserved.

**R37. New settings get documented defaults.** _(Nice-to-have)_ When an update introduces a setting that did not previously exist, the new setting takes a documented default and the user is informed of its addition on first launch after the update.

**R38. Behavioural changes called out at update time.** _(Essential)_ When an update changes default behaviour in a way that would alter what the user’s keyboard does today, the change is surfaced in release notes and called out at update time.
