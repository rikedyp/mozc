# Ctrl+Key APL Input: Electron/Chromium Limitation

## Status: Known Issue (2026-02-17)

## Summary

APL mode persistence (Step 2.1) is working correctly. The `apl_mode_active_`
flag survives Enable/Disable/FocusIn/FocusOut cycles, and the property handler
consistently reports `mode=6` (APL) throughout. Ctrl+key events reach mozc and
are consumed (`consumed=1`).

However, **Electron/Chromium apps (including VSCode) intercept Ctrl+key
shortcuts independently of the IME layer**, so Ctrl+A triggers "select all"
even though mozc consumed the key event and returned `consumed=true`.

## Evidence

### Kate (GTK/Qt, works correctly)

```
FocusIn activated=1 mode=6
ProcessKeyEvent: keyval=97 mod=4 consumed=1 out_mode=-1   # Ctrl+A -> alpha
ProcessKeyEvent: keyval=112 mod=4 consumed=1 out_mode=-1  # Ctrl+P -> pi
ProcessKeyEvent: keyval=108 mod=4 consumed=1 out_mode=-1  # Ctrl+L -> quad
```

APL glyphs are output. No application shortcuts triggered.

### VSCode (Electron, broken)

```
FocusIn activated=1 mode=6
ProcessKeyEvent: keyval=97 mod=4 consumed=1 out_mode=-1   # Ctrl+A consumed...
FocusOut                                                    # ...but VSCode did "select all"
Disable                                                     # and then disabled the IME
```

Mozc consumed the key, but VSCode processed Ctrl+A as "select all" anyway.
The FocusOut/Disable immediately after suggests VSCode's handling of the
shortcut caused a focus change or IME reset.

## Root Cause

GTK apps and Chromium/Electron handle IME key events very differently.

### GTK apps (works correctly)

GTK calls `gtk_im_context_filter_keypress()` **synchronously** before doing
anything else with the key:

```
Key press (Ctrl+A)
  → gtk_im_context_filter_keypress()   ← blocks waiting for IBus
      → IBus daemon → mozc → consumed=true
  ← GTK gets TRUE
  → key is dropped; app never sees it
```

### Chromium/Electron (broken for Ctrl+)

Chromium doesn't use `GtkIMContext`. It has its own IBus client
(`ui/base/ime/`) that runs independently of the accelerator system:

```
Key press (Ctrl+A)
  ├─→ Chromium accelerator table       ← checked FIRST, synchronously
  │     "Ctrl+A = select all" → executes immediately
  │
  └─→ IBus D-Bus call (lower priority / async)
        → mozc → consumed=true
        ← response arrives... but select-all already fired
```

The accelerator check runs at the browser-process level *before* the event
reaches the IME subsystem. `consumed=true` from mozc only prevents a duplicate
character being inserted — it cannot undo a shortcut that already fired.

This is a known architectural limitation of Chromium's IME integration on
Linux. There have been many upstream Chromium bugs filed about it.

## Possible Workarounds

### Option A: Accept the limitation
Ctrl+key APL input works in native apps (Kate, terminals, etc.) but not in
Electron apps. Users would need a different workflow for VSCode.

### Option B: Configurable shifting key (Step 2.3) ← recommended
Allow using Right-Alt (or another modifier) instead of Ctrl. Right-Alt is not
in Electron's accelerator table, so the key reaches the IME cleanly.
Already planned as Step 2.3 in the design doc. Right-Alt is a natural choice:
- Not used by VSCode/Electron for any default shortcuts
- Used by some existing APL implementations (e.g. Dyalog on Windows)
- Ergonomically similar to Ctrl for touch-typing

### Option C: Wayland (may fix it properly)
The Wayland input-method protocol (`zwp_input_method_v2`) is *supposed to*
route all key events through the IM **before** the application sees them, not
in parallel. If Electron on Wayland respects this, Ctrl+key would reach mozc
first and the accelerator would never fire. Worth testing — if it works, no
code changes are needed on the mozc side.

### Option D: X11 `XGrabKey`
Grab Ctrl+key combinations at the X server level so they never reach the
application. Effective but very invasive — the grab is global, meaning Ctrl+A
would stop working as "select all" in *all* apps while APL mode is active.
Some Chinese IMEs use this approach; generally considered too disruptive.

### Option E: VSCode extension
A small VSCode extension intercepts key events at the editor level and inserts
APL glyphs directly, bypassing the IME entirely. VSCode-specific but reliable.
Could coexist with the mozc IME approach for other apps.
