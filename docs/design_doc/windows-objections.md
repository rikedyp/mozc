Reusing the same Profile GUID for English blocks future dual-profile (Japanese + APL) — When you later want both, you'll need a new GUID for the APL profile, and users who installed the POC will have orphaned English-language registry entries under the old GUID that conflict with re-registering it as Japanese.

No CompositionMode::APL on Windows — macOS added APL = 6 to the shared proto and wired it through session/keymap/transliteration; this POC sidesteps that entirely by replacing Japanese, so the glyph insertion phase will need to retrofit composition mode integration that should have been designed in from the start.

W3 claims independent testability ("clicking toggles checkmarks, visual only") but the existing OnMenuSelect enforces mutual exclusion — You can't demo checkmark toggling without first gutting the radio logic, so W3 and the selection-model rewrite are actually one atomic step.

Proto caps_lock field 3 is shared cross-platform but the semantics differ — macOS treats Caps Lock as a modifier (must intercept before KeyCodeMap.mm strips it), Windows treats it as a toggle key (must eat VK_CAPITAL to suppress LED); sharing the field implies behavioral equivalence that doesn't exist.

Enabling Left Alt in the menu with no bare-press suppression is a user-hostile trap — The POC lets users check "Left Alt" but defers the OnKeyUp eating to the glyph phase, so selecting it actively breaks Alt menu-bar access with zero benefit until glyph insertion ships.

ConfigSnapshot uses GetSharedConfig() (shared memory) while menu toggle uses client->GetConfig/SetConfig (IPC to server) — These are two different config access paths that may not be immediately consistent, risking stale reads after writes.

All language bar changes are throwaway work — A production version supporting both Japanese and APL will need the input mode toggle and the APL menu coexisting (or a mode-dependent menu), so the W3 rewrite that rips out the Japanese menus will be entirely undone.

No discussion of the EnsureKanaLockUnlocked and Japanese-specific key handling code paths — The plan says they "become no-ops" under English, but doesn't verify whether any of them check the registered language ID and would error or behave unexpectedly when it's 0x0409 instead of 0x0411.