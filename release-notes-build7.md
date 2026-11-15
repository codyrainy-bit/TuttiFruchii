# TuttiFruchii build 7

Windows x86_64, clang-cl Release with PGO (`lichii-windows.profdata`), built for **x86-64-v3 (AVX2)**.
Requires a 2013-or-newer CPU with AVX2.

## What is in it

**Base:** Lichii build-160 (`2eb5f081c8`) with the Ocarina of Time performance line (#1703) and list-indexed
draws / draw write extent (#1559).

**Shader stutter:** #1366 precompiled pipelines are in. Pipelines are prepared in the background from your own
play and stored in `%APPDATA%\lichii\cache\shader\<program id>\`; the first session still compiles what it sees.

**Upstream nx2 sync:** 21 upstream PRs, including #1796 (ASTC→BC staging buffers), #1784 (NCA sections opened
on first use), #1783 (`GetSpan` per-page host offset), #1766 (no null view for unbacked render targets),
#1757 (NVIDIA global memory buffer via `OpSelect`), #1751 (dynarmic fault reporting), #1704 (MKW 60 FPS),
#1743/#1745 (OoT FPS-Unlock cutscene and Scarecrow's Song), #1731/#1733/#1736/#1737/#1742/#1744/#1747/#1750/
#1753/#1771/#1779.

**#1811:** vk_scheduler GPU timestamps, behind `EDEN_DKB_GPU_SITES` (off unless you set it).

**#1798:** two of its three commits, `1b7032076c` (sRGB resample in linear light, no guest-extent copies) and
`90478ba107` (uneven-mip ASTC re-compress, AMD only). The third, the decode-image reuse, is **not** in this
build: it handed out decode images from `pending_transcodes` while those transcodes were still in flight, and
it caused `VK_ERROR_DEVICE_LOST` on NVIDIA after 40–70 seconds in Ocarina of Time.

**Stability patch v3:** guest memory is backed by a sparse temporary file instead of a section committed in
full at boot. **On by default now.** It still falls back to the committed section when the temporary drive
lacks the space, the filesystem cannot do sparse files, or a writable guest allocation has been handed out.
`EDEN_NX2_DYNAMIC_MEMORY=0` forces it off.

**Low VRAM:** the Fritchii #55 defaults, including ASTC trim when the reported budget is under 5 GiB.

**Name and icon:** the product is now TuttiFruchii, the binary is `tuttifruchii.exe`, and it carries the new
icon. The data directory is still `%APPDATA%\lichii`, so an existing config, saves and precompiled store keep
working.

## Things to test

1. **Shader stutter.** Play somewhere new; the second visit to the same area should be smoother than the first.
2. **Kokiri Forest water in Ocarina of Time.** Load a save, stand in the water, stay 3–5 minutes. This is
   where the previous build crashed with `DEVICE_LOST`.
3. **Cutscenes and dialogue in areas you have not visited.** Shader compilation there is the densest.
4. **Low-VRAM path forced on:** `EDEN_NX2_ASTC_TRIM=1`, boot OoT, confirm it renders and does not leak.
5. **Sparse guest memory:** if the game starts normally, the fallback did not trigger; check the log for
   `Dynamic guest memory` to see which path was used. `EDEN_NX2_DYNAMIC_MEMORY=0` compares against the old
   behaviour.

## Known limitations

- No Android build.
- The social-preview image on the repository is not set; the files are in `dist/branding/`.
- The precompiled store was rebuilt for this build (info format changed to NX2INFO7), so the first run after
  an older build starts with an empty store.
