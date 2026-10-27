# 0.4.15-HD: frame-121 callback crash

Device 0.4.14-HD-Perf logs returned 120 frames, began frame 121, then reported `bad_function_call`. The native GL error at exit was zero. Configuration was 1280x720 and lighting 0.25; HD replacement reported no rejection/budget skip. These logs do not indicate save corruption.

Cause: Switch presentation captured the local GraphicsBackend by reference. That backend was moved into lifecycle_probe; after the first 120 timing intervals, presentation called the moved-from graphics.record function. The 0.4.14 Windows game host did not execute this Switch-specific presentation code, so its 1220-frame runs missed the defect.

Fix: shared FrameTiming owns a copy of the log callback, independently of backend movement. Switch presentation calls that object. An absent callback is safe. The same helper is directly exercised by a regression that moves GraphicsBackend, clears the source callback deterministically and crosses frames 121, 241 and 361, checking exact interval values and multiple reporting windows; an empty-sink case also passes.

Keep 0.4.14's render optimizations, default 720p, base-mip compatibility, 53 selected HD textures, original fallback, inputs and saves. No save reset is part of the fix. Source regression and Switch compile pass; NSP readback is recorded privately. Actual installation/gameplay on Switch and FPS improvement remain pending. Do not present this as full campaign validation.
