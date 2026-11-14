# Lichii build-160 + PR #1161 + #1366 + #1449 + #1450

Source tree used for the "second build" described by the original author:

> I started my branch from **build 160**. Threw some PRs in it, added some modifications,
> and did my PR **#1161**. That's my first build. Then I picked this same branch of my
> PR #1161, created another branch, and threw **#1366, #1449 and #1450** in it.
> That's my second build.

## Bases

| Thing | Commit |
|---|---|
| `build-160` (tag) | `2eb5f081c8` — "network: don't log would-block socket reads (#1384)" |
| PR **#1161** head (Ocarina water, replaces #1069) | `3039a0f131` |
| PR **#1366** head (precompiled pipelines) | `d5cf839a5c` (precompile side `0b08951bd6`) — based on mainline `fc2ce2f64e` |
| PR **#1449** ("disconnect OnEmulationStopped before terminating") | `3a0006ac0f` |
| PR **#1450** ("suspend guest threads before powering off") | `8927aedcb9` |

## Branches

| Branch | What | Builds |
|---|---|---|
| **`merge-1366`** | **The second build**: branch off the #1161 work, then #1366 merged in. #1449/#1450 are already present via #1366's mainline side. The binaries were built from commit `9b5e963807` (this file is the only later change). | yes, tests pass |
| `pr1161-as-is` | The first build: `build-160` + #1161 only. | yes, tests pass |
| `oot-1161-plus-1366` | Attempt to hand-port #1366 onto #1161 keeping #1161's colliding nx2 internals. Parked (errors concentrated in `vk_graphics_pipeline.h`). | no |
| `with-1161-full` | earlier WIP | no |

Note: on files where #1161 and newer mainline re-implement the *same* nx2 code, a merge can
only keep one side; `merge-1366` keeps mainline's (which already contains the same
Ocarina-water drain optimisations — the identical "Kokiri village" wait reasoning lives in
`gpu_thread.cpp` / `memory_manager.cpp` upstream).

## Build recipe (Windows, clang-cl)

```
clang-cl 23.1.5 (C:\Program Files\LLVM\bin)
Ninja, CMAKE_BUILD_TYPE=Release
Qt 6.8.3 shared  ->  CMAKE_PREFIX_PATH=C:/lx/qt/6.8.3/msvc2022_64
```

`-DLICHII_USE_BUNDLED_QT=OFF` is required with a *shared* Qt: when it is `ON`
(the MSVC default) it forces `LICHII_STATIC_BUILD` -> `QT_STATICPLUGIN`, which expects a
static Qt and otherwise fails to link
(`undefined symbol: qt_static_plugin_QWindowsIntegrationPlugin`, `vmaAllocateMemory`).

PGO + ThinLTO, reusing the profiles shipped in the source tree
(`lichii-windows.profdata`, trained on Ocarina of Time), exactly as the release CI does:

```
CMAKE_CXX_FLAGS = -DWIN32 -D_WINDOWS -EHsc -DNOMINMAX -D_USE_STD_VECTOR_ALGORITHMS=0
                  -flto=thin
                  -fprofile-use=<repo>/lichii-windows.profdata
                  -Wno-error=profile-instr-out-of-date
                  -Wno-error=profile-instr-unprofiled
                  -Wno-error=backend-plugin
```

(`eglantine.profdata` is the Linux profile. `-Wno-error=profile-instr-out-of-date` is
expected: the profile predates the source.)

Targets:

```
cmake --build build --target lichii tests
```

## Output

```
build\bin\lichii.exe     58,781,696 bytes
build\bin\tests.exe      58,750,976 bytes
```

with the Qt runtime deployed beside them (`Qt6*.dll`, `plugins\`, `qt.conf`,
`dxcompiler.dll`, `dxil.dll`).

## Tests

```
tests.exe "[canonical_native_key],[fast_draw],*Precompil*,*precompile*,*Warm*"
  -> All tests passed (227 assertions in 29 test cases)

tests.exe "[nx2]"
  -> All tests passed (2431 assertions in 10 test cases)
```
