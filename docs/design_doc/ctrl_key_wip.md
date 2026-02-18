# Ctrl+Key APL Input: XWayland Limitation

## Status: Resolved on Wayland; XWayland limitation documented (2026-02-18)

## Summary

APL mode persistence (Step 2.1) is working correctly. The `apl_mode_active_`
flag survives Enable/Disable/FocusIn/FocusOut cycles, and the property handler
consistently reports `mode=6` (APL) throughout. Ctrl+key events reach mozc and
are consumed (`consumed=1`).

**On a native Wayland session, Ctrl+key APL input works correctly in VSCode**
and other Electron/Chromium apps. The Wayland compositor routes key events
through the IME before the application's accelerator table, so `consumed=true`
from mozc correctly suppresses the shortcut.

The problem is specific to **XWayland compatibility mode** — apps running under
X11 emulation on Wayland, or in a plain X11 session. In that case, Electron's
accelerator table fires before the IBus D-Bus response arrives, and
`consumed=true` cannot undo the shortcut.

## Evidence

### Kate (GTK/Qt, works correctly)

```
FocusIn activated=1 mode=6
ProcessKeyEvent: keyval=97 mod=4 consumed=1 out_mode=-1   # Ctrl+A -> alpha
ProcessKeyEvent: keyval=112 mod=4 consumed=1 out_mode=-1  # Ctrl+P -> pi
ProcessKeyEvent: keyval=108 mod=4 consumed=1 out_mode=-1  # Ctrl+L -> quad
```

APL glyphs are output. No application shortcuts triggered.

### VSCode on Wayland (works correctly)

Verified 2026-02-18 on KDE Plasma Wayland session. Ctrl+key produces APL glyphs
in VSCode identically to Kate. The compositor intercepts keys before Electron's
accelerator table.

### VSCode on XWayland / X11 session (broken)

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

The difference is not GTK vs Electron — it is **Wayland vs XWayland**. On native
Wayland, the compositor routes key events through the IME (`zwp_input_method_v2`)
before the application sees them, so `consumed=true` works correctly everywhere.
On XWayland, the X11 event model is used: GTK apps call
`gtk_im_context_filter_keypress()` synchronously before their shortcut handlers,
but Chromium/Electron checks its accelerator table independently and in parallel
with the async IBus D-Bus call.

### GTK apps on X11/XWayland (works correctly)

GTK calls `gtk_im_context_filter_keypress()` **synchronously** before doing
anything else with the key:

```
Key press (Ctrl+A)
  → gtk_im_context_filter_keypress()   ← blocks waiting for IBus
      → IBus daemon → mozc → consumed=true
  ← GTK gets TRUE
  → key is dropped; app never sees it
```

### Chromium/Electron on XWayland/X11 (broken for Ctrl+)

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
X11/XWayland. There have been many upstream Chromium bugs filed about it.

## Resolution

**Option C (Wayland) is confirmed working.** Verified 2026-02-18: on a native
KDE Plasma Wayland session, Ctrl+key APL input works correctly in VSCode.
No mozc changes are needed — the compositor handles the routing.

The recommended user requirement is therefore: **run a Wayland session**.
This is the default on modern KDE and GNOME installs.

## Remaining Limitation: XWayland apps

Apps that run in XWayland compatibility mode (e.g. older Electron versions that
do not support `--ozone-platform=wayland`) still exhibit the broken behaviour
even within a Wayland session. The specific known case is **Ride** (Dyalog APL
IDE), which uses an older Electron version.

**Decision**: Accept this limitation. Ride is expected to update its Electron
version before this IME ships. The limitation will be documented in the user-
facing release notes. No workaround is planned at this time.

If a workaround is needed in future, options remain:
- **Configurable shifting key** (Step 2.3): Right-Alt is not in Electron's
  accelerator table and would work even on XWayland.
- **VSCode extension**: intercepts keys at the editor level, VSCode-specific.
- **X11 `XGrabKey`**: global grab, too invasive for general use.
