# 0.4.13-HD compatibility release

The user reproduced Chapter 2 armor noise in the baseline and modes A/B. Mode C (base-mip linear sampling) removed it, with no other texture issue noticed during their test. This is device feedback for 0.4.12, not full-campaign validation of 0.4.13-HD.

Production behavior:
- Default to the same reversible base-mip override on mipmapped minification filters, while keeping original filters and active texture units restored after each draw.
- Ignore render-test.cfg. Do not omit blended UI or synchronize every draw. Remove the diagnostic splash and three-second delay.
- mipmap-fix.cfg accepts 0/1; missing or invalid defaults to 1.
- hd.cfg accepts 0/1; missing or invalid defaults to 1. Load only the exact-key embedded HD selection. Existing 128 MiB replacement budget, dynamic/corrupt/missing resource fallbacks and original texture resources remain.
- The user-provided earlier packaging kit has 55 exact matches. Use the existing prepare_hd_subset tool to cap textures at 2048 through existing mip levels; exclude one texture for which the cap provides no upscale. The local package contains 54 HD textures, not a full remaster.
- Saves and input mapping retain the previous implementation. No saves or keys are bundled.

Validation:
- Toggle parser tests and both diagnostic/production mip state restoration tests pass, including a throwing draw.
- Actual Mesa llvmpipe GL readback suite passes with the production flag off and on, including synthetic HD mip upload, binding restoration, RGB semantics and dynamic/corrupt/budget fallback. This is desktop GL, not Switch hardware.
- All 54 selected real KTX files parse and all 577 levels decode with dimensions/lengths checked.
- devkitA64 r30/GCC 16.1/libnx 4.12 cross-compilation and final NRO packaging are recorded in the delivered verification report. The new HD-enabled combination has not been exercised in the actual game or on Switch hardware.

Rebuild with the pinned patched Dynarmic, normal CPU runtime build, and:
make -C switch CPU_PROBE=1 GAME_LAUNCHER=1 APP_VERSION=0.4.13-HD ROMFS_ROOT=/private/staged/romfs

Only runtime sources and this document are public. Game library, assets and HD files are delivered privately using the user's supplied package.
