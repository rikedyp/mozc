# Project Dependencies

All git submodules are for the legacy GYP build path (deprecated on Linux, maintenance mode on Windows/macOS) and are unnecessary when building exclusively with Bazel.

## All Platforms

Pre-installed:

- **Git** — Version control; needed to clone the repository
- **Bazelisk** — A launcher that automatically downloads and runs the correct version of Bazel (8.4.1) as specified in `src/.bazeliskrc`. Bazel is the build system that compiles, tests, and packages the entire project
- **Python 3.12 or later** — Runs build helper scripts (`update_deps.py`, `build_qt.py`, etc.) that download, extract, and configure dependencies outside of Bazel

Fetched automatically by Bazel (from `src/MODULE.bazel`):

- **abseil-cpp 20250814.0** — Google's C++ standard library extensions (string utilities, synchronisation primitives, containers, etc.). Used pervasively throughout Mozc's C++ code
- **protobuf 32.0** — Google's Protocol Buffers serialisation framework; used for IPC between client/server, configuration storage, and dictionary data formats
- **googletest 1.17.0** — Google's C++ testing and mocking framework; used for all unit tests
- **bazel_skylib 1.8.1** — Standard library of utility functions and rules for Bazel build files (not runtime code)
- **platforms 1.0.0** — Bazel's platform definitions for cross-compilation targeting (e.g. Windows x64 vs ARM64)
- **rules_cc 0.2.2** (patched) — Bazel ruleset for compiling C/C++ code; includes toolchain auto-detection. Patched to support 32-bit Windows builds with clang-cl
- **rules_pkg 1.1.0** (patched) — Bazel ruleset for creating distribution packages (zip, tar, etc.); used to produce `mozc.zip` on Linux and installers on other platforms
- **rules_python 1.5.4** — Bazel ruleset for running Python scripts and managing Python toolchains within the build
- **Material Design Icons 4.0.0** (PNG/SVG) — Google Material icon files used in the Mozc UI (dictionary, properties, and tool icons)
- **Japanese Usage Dictionary** (2025-01-25) — Dictionary of Japanese word usage examples; used by the suggestion/conversion engine to rank candidates based on real-world usage patterns
- **Japan Post zip code data** — Japanese postal code CSV files (business and general addresses) that enable zip-code-to-address conversion in the IME

## Windows

Pre-installed:

- **Visual Studio 2022 Community Edition** — Provides the MSVC compiler, linker, and Windows SDK headers/libraries needed to build Windows-native binaries
  - **Windows 11 SDK** — Windows API headers, libraries, and tools
  - **MSVC v143** - VS 2022 C++ x64/x86 build tools — The C++ compiler and linker toolchain
  - **C++ ATL for latest v143 build tools** (x86 & x64) — Active Template Library; used by Mozc's Windows IME integration (TSF/COM components)
  - (ARM64 builds also need: MSVC v143 ARM64/ARM64EC + C++ ATL ARM64/ARM64EC)
- **.NET 6 or later** — Provides the `dotnet` CLI, needed solely to install and run the WiX toolset
- **WiX 5.0.2** — Windows Installer XML toolset; compiles `.msi` installer packages for distributing Mozc on Windows. Installed via `dotnet tool restore`

Downloaded by `update_deps.py`:

- **LLVM 20.1.1** (clang-cl, lld-link, llvm-lib) — Provides the MSVC-compatible C++ compiler, linker, and archiver. Used as the primary compiler for the Bazel build on Windows instead of MSVC's `cl.exe`
- **MSYS2 2025-02-21** — A minimal Unix-like environment for Windows; provides shell utilities (bash, coreutils, etc.) that Bazel and build scripts need on Windows
- **Ninja 1.11.0** — A fast, low-level build system used specifically to compile Qt from source (not for Mozc itself)
- **Qt 6.9.1 source** — Cross-platform GUI framework; provides the UI for Mozc's configuration tool, candidate window renderer, and other GUI components. Downloaded as source and compiled locally by `build_qt.py`

Fetched by Bazel:

- **Windows Implementation Libraries (WIL) 1.0.230629.1** — Microsoft's header-only C++ library providing type-safe wrappers around Windows API patterns (COM pointers, handles, error handling, registry access, etc.)

## Linux

Pre-installed (apt packages, reference environment Ubuntu 24.04):

- **GCC** (or Clang) — The C++ compiler and linker
- **libibus-1.0-dev** — Development headers for IBus (Intelligent Input Bus), the input method framework on Linux desktops. This is how Mozc integrates with the operating system's text input pipeline. Also pulls in glib-2.0 and gobject-2.0 (GLib's object system, used by IBus)
- **qt6-base-dev** — Pre-built Qt6 development package (same GUI framework as on Windows/macOS, but installed from the system package manager rather than compiled from source). Provides Qt6Core, Qt6Gui, Qt6Widgets

## macOS

Pre-installed:

- **Xcode 16.0 or later** — Provides the Apple Clang compiler, macOS SDK, and code signing tools. Full Xcode is required (not just Command Line Tools) because the build uses Xcode's build infrastructure for macOS-specific components
- **CMake 3.18.4 or later** — A build system generator used specifically to configure and compile Qt6 from source on macOS

Downloaded by `update_deps.py`:

- **Ninja 1.11.0** — A fast, low-level build system used to compile Qt from source
- **Qt 6.9.1 source** — Cross-platform GUI framework, compiled from source for macOS by `build_qt.py`

Fetched by Bazel:

- **rules_apple 4.1.2** — Bazel ruleset for building macOS/iOS applications, including bundling `.app` packages and `.pkg` installers
- **apple_support 1.23.1** — Bazel support library for Apple platform toolchain configuration (Xcode detection, SDK selection, etc.)
- **rules_swift 3.1.2** — Bazel ruleset for Swift compilation. Mozc doesn't use Swift directly, but rules_apple depends on it, and version 2.5.0+ is required to avoid build failures on Windows
- **Google Toolbox for Mac** — Google library providing Objective-C testing utilities; only the UnitTesting component is used, for macOS-specific unit tests

## Android NDK

`update_deps.py` downloads Android NDK r29 on Linux and macOS by default. The NDK provides the cross-compilation toolchain (Clang for ARM/x86 Android targets) needed to build Mozc's C++ core as a native library for Android. This is only needed for Android builds and can be skipped with the `--nondk` flag.

## APL Edition Dependency Changes

### Clearly removable (Japanese-specific data)

| Dependency | Reason |
|---|---|
| **Japanese Usage Dictionary** | Japanese word usage examples for ranking candidates. An APL IME has no use for this. Only referenced during dataset compilation. |
| **Japan Post zip code data** | Japanese postal code lookup. Entirely optional (gated by a config flag). No relevance to APL. |

### Likely removable (not needed for APL use case)

| Dependency | Reason |
|---|---|
| **Google Toolbox for Mac** | Only used for macOS Objective-C unit tests. Can be dropped if you don't run those specific tests. |
| **Android NDK** | Already skippable with `--nondk`. Not needed for desktop-only APL edition. |
| **breakpad** | GYP-only; already absent from the Bazel build. No action needed. |

### Replaceable but not removable

| Dependency | Reason |
|---|---|
| **Material Design Icons** | Not Japanese-specific (generic settings/dictionary/tool icons), but should be replaced with APL-relevant icons. The build already supports swapping icons via `mozc_select()`. |
| **IBus / libibus-1.0-dev** | Not Japanese-specific — it's the generic Linux input method framework. Still needed (or an alternative like Fcitx) for Linux IME integration. |
| **Qt6** | The GUI framework for the config dialog, candidate window, etc. Still needed unless all GUI is stripped. |

### Cannot realistically remove (deeply embedded)

| Dependency | Reason |
|---|---|
| **abseil-cpp** | Used in 59% of all C++ source files (602 of 1,017). Provides string_view, Status/StatusOr, containers, synchronisation, and flags throughout. Removing it would mean rewriting ~600 files. |
| **protobuf** | Defines the core IPC protocol between `mozc_server` and all clients (`commands.proto`). 227 source files import protobuf headers. Replacing it is theoretically possible but would be a major rewrite. |
| **googletest** | Standard test framework. No reason to remove unless all existing tests are abandoned. |
| **Bazel + all rules_* rulesets** | The entire build system. All Bazel rulesets (rules_cc, rules_pkg, rules_python, bazel_skylib, platforms, and the macOS-specific rules_apple/apple_support/rules_swift) are build infrastructure, not runtime dependencies. |
| **All platform toolchains** | Visual Studio, LLVM, MSYS2, Ninja, Xcode, CMake, GCC, .NET, WiX, and WIL are all required for their respective platforms regardless of the IME's target language. |

## Licensing
The mozc project and its dependencies are made available under a variety of licences. See `docs/project-licensing.md` for a full analysis of licence obligations for fork distribution.
