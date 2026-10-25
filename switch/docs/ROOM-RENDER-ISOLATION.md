# Room render isolation candidate (ROOM-TEST-1)

This source candidate does not claim the Chapter 2 armor noise is fixed.
Based on 2c55047, retaining driver-owned client buffers and existing exposure controls.

`sd:/switch/deadspacemobile/render-test.cfg` accepts exactly one integer:

- 0 (default/missing/invalid): baseline.
- 1: omit ALL blended draw calls, including transparent surfaces, effects and possibly UI. This is deliberately broad, not a player-only reflection filter. Missing graphics are expected. If the affected armor itself disappears, the result is inconclusive.
- 3: temporarily use linear base-mip sampling on textured units that request mipmapped filtering. Restore each filter and the active unit after drawing, including draw exceptions. This tests mip-related behavior; it does not replace textures, rebuild a texture cache, or prove mip corruption.
- 2: call glFinish after every submitted draw. This can be extremely slow and is diagnostic only. Improvement would implicate synchronization-sensitive behavior, not prove a specific driver bug.

Use the same lighting.cfg value (recommended 0.25), resolution, save and program for all comparisons. Fully close and reopen each time. Test an outside save followed by entry and an inside save. Do not delete saves.

Startup console shows ROOM-TEST-1, build version, mode, lighting strength and GPU buffers for three seconds before entering graphics. Both logs receive a mode marker; the runtime separately logs the effective mode and first affected draw. Startup readout describes configured values, not proof that a particular draw was reached.

Validation in this environment: C++17 compile with Wall/Wextra/Werror; executable mock-backend regression checks baseline draws, omission only with blending enabled, glFinish calls and counters. Not a pixel/real-game test. Switch compilation, NSP packaging, real donor execution and hardware tests were not performed: devkitPro and the user's donor resources are unavailable here.

No game assets, keys or saves are included. Do not present this source patch as an installable NSP.

Follow-up validation:
- Two-unit mip-filter mock test passes for success and thrown draw; all filters and active unit restored.
- Baseline/omission/synchronization regression passes after the mip-path addition.
- Existing native GL test fixture updated to named GraphicsBackend fields (old positional initializer no longer matched the struct). Real Mesa llvmpipe GL readback suite passes: arrays/buffers, compressed uploads, HD mip/fallback, RGB semantics and texture bindings. This suite does not load the game or reproduce the Chapter 2 issue.
