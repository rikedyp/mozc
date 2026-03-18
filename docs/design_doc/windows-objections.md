ConfigSnapshot uses GetSharedConfig() (shared memory) while menu toggle uses client->GetConfig/SetConfig (IPC to server) — These are two different config access paths that may not be immediately consistent, risking stale reads after writes.

All language bar changes are throwaway work — A production version supporting both Japanese and APL will need the input mode toggle and the APL menu coexisting (or a mode-dependent menu), so the W3 rewrite that rips out the Japanese menus will be entirely undone.

No discussion of the EnsureKanaLockUnlocked and Japanese-specific key handling code paths — The plan says they "become no-ops" under English, but doesn't verify whether any of them check the registered language ID and would error or behave unexpectedly when it's 0x0409 instead of 0x0411.