# Remove mobile (Android/iOS) — PR doc

- **Work unit:** Mobile removal — a work unit of the [Trim build/CI & rebuild
  tests](../../PLAN.md) epic (PLAN.md Migration step 8). Rationale: REWRITE.md §2.6.
- **Branch:** `remove-mobile-android-ios` (off `apl-planning`).
- **Status:** **Scoped, then deferred to its proper slot (Migration step 8).**
  This is an order-free leaf-node prune, but the plan executes it in step order
  rather than bringing it forward. The `remove-mobile-android-ios` branch is
  reserved for the deletion; the scoping below stands and is executed when the
  migration reaches step 8. No code has been deleted yet.

## Summary

Remove the Android and iOS front-ends, which are not in the v1 platform list and
are leaf nodes (no desktop target depends on them). Also remove the
mozc-specific Android NDK build configuration. Keep the build green for the
Linux/macOS/Windows targets throughout.

## Findings that corrected REWRITE.md §2.6

Two inaccuracies in the original assessment, discovered while scoping:

1. **`MODULE.bazel` cannot drop `rules_go` / `gazelle` / `rules_android`.** Their
   own comments state they are *not* mozc dependencies — they are forced
   version-overrides of **protobuf's** transitive chain
   (`protobuf → rules_jvm_external → rules_android → rules_go`) for Bazel-9
   compatibility. We keep protobuf, so these **must stay**. Only the
   Android-NDK-specific group is removable.
2. **The android references are more widespread but mostly already inert.**
   `mozc_select(android = …)` branches in `engine/`, `converter/`, `session/`,
   and `data_manager/` reference targets such as `//data_manager/android:…` and
   `//engine:android_engine_factory` that **do not exist in this OSS tree**.
   The Linux baseline builds green with them present, which empirically confirms
   Bazel does not require targets in *unselected* `select()` branches to exist.
   These pre-existing dangling refs are therefore harmless on desktop and are
   **left for the engine-strangle phase** (when we are already editing those
   files) rather than churned here.

## Scope — what changes

**Delete (real, existing mobile code):**
- `src/android/` (jni/mozcjni.cc, make_archive.py, cross_build_binary.bzl,
  BUILD.bazel; gen_touch_event_stats.py; collected_keyboards.csv)
- `src/ios/` (ios_engine*.cc/.h, ios_engine_stub.cc, BUILD.bazel)
- `.github/workflows/android.yaml`
- `src/bazel/android_repository.bzl` and `src/bazel/rules_android_ndk.patch`
  (Android-NDK build infra; referenced only by the removed MODULE block)

**Edit `src/MODULE.bazel` — remove the Android-NDK group ONLY (keep Group 1):**
- `rules_android_ndk` `bazel_dep` + its `single_version_override`
- the `android_ndk_repository_extension` `use_extension` + `.configure(...)` +
  `use_repo(...)` block
- `register_toolchains("@androidndk//:all")`
- Update the `rules_go`/`gazelle`/`rules_android` comments to note they are
  retained deliberately as protobuf transitive overrides (not mobile leftovers).

**Edit — drop references to the now-deleted *real* targets (avoid new dangling refs):**
- `src/BUILD.bazel:303` — remove `android = ["//android/jni:native_libs"]` from
  the `package` filegroup `mozc_select`.
- `src/session/BUILD.bazel:156-157` — remove `//android/jni:__pkg__` and
  `//ios:__pkg__` visibility grants.
- `src/engine/BUILD.bazel:88` — remove `//ios:__pkg__` visibility grant.
- `src/data_manager/BUILD.bazel:53,58` — remove the two visibility grants.

**Explicitly NOT touched in this unit (deferred to the engine-strangle phase):**
- Pre-existing dangling `mozc_select(android=…)` branches in `engine/`,
  `converter/`, `session/`, `data_manager/` BUILD files (already inert on desktop).
- `noandroid` tags scattered across `session/` and `renderer/` BUILD files
  (harmless; cosmetic cleanup).

## Verification (definition of done)

- `bazelisk build //unix:package --config release_build` succeeds (Linux package
  links — proves no real target lost a needed dep).
- `bazelisk test base/... composer/... session/... --build_tests_only -c dbg`
  stays green (regression check vs. the 191/191 baseline).
- `bazelisk query //...` loads with no *new* unresolved labels attributable to
  this change (pre-existing android dangling refs excepted).

## Design decisions

- **Keep protobuf's transitive overrides** (`rules_go`/`gazelle`/`rules_android`) —
  removing them would break protobuf on Bazel 9. (Correction to REWRITE §2.6.)
- **Minimal, focused diff:** remove only mobile code that exists + direct refs to
  it; defer the inert engine/converter/data_manager android-select cleanup to the
  phase that already edits those files. Keeps this unit low-risk and green.

## Commits

- _(pending review)_
