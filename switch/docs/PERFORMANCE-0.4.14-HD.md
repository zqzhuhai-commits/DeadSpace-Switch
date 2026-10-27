# 0.4.14-HD performance candidate

Based on 0.4.13-HD commit aa8adeda048c9f1177013b9429b0eed6eaeb2b48, incorporating the earlier 0.4.12 performance work. Preserve the production base-mip compatibility override, default 1280x720, 128 MiB HD budget, exposure setting, input mapping, saves and lack of a custom loading overlay. No overclock or gameplay time changes.

Changes:
- Resolve native GL entry points once per bridge/context; reuse bounded client array/index staging capacity while reading fresh guest data on every draw.
- Cache the queried original minification filter by original texture object. Invalidate on any guest TexParameter setter and texture deletion. Continue applying/restoring the base-mip override per draw, including exception recovery. New HD objects receive original parameters through the existing dirty-state synchronization.
- Record mean/max displayed-frame intervals and presentation time every 120 frames on Switch. This is not a GPU timer; loading/pause can affect the window.

Validation:
- Mip regression covers two units, successful and throwing draws, zero repeat queries for unchanged textures, one re-query after a setter and deletion/name reuse. Render-isolation/settings tests pass.
- Native GLES1/Mesa llvmpipe pixel tests retain both client-buffer paths, fresh index contents, fixed-function and binding-restoration checks.
- Two sequential real-library host runs each returned 1,220 frames with base-mip compatibility and HD enabled, entered the save scene without runtime errors, and loaded 18 HD matches/189 mip levels. First run used the host's former 192 MiB test budget; second uses the same 128 MiB as Switch. These software-rendered host runs do not measure Switch FPS or prove the hardware noise fixed.
- Local HD selection: 53 textures, 565 structurally checked mip levels, all source hashes verified, maximum dimension 2048. This differs from the 54 stated in 0.4.13's document: that NSP is unavailable locally. Unmatched textures retain the original fallback. Do not label this package byte-identical to 0.4.13's resources.
- Switch cross-build and NSP readback are recorded in the private delivery report. Hardware/emulator testing, FPS gain, repeat-save boot and later levels/elevators remain pending.

Publish runtime sources only. No game library/assets, keys, saves or integrated NSP are uploaded publicly.
