# 0.3.9 performance candidate

The 0.3.8 hardware report describes slow rendering, broken-up sound and sluggish
sticks. No hardware timing log was supplied for that report, so the dominant
hardware bottleneck is not established.

Changes:
- Enable Dynarmic's page table only for fully mapped, permanently resident heap
  and main-thread stack pages. Pages are materialized on checked access, shared
  with all host imports, and remain stable across unordered-map rehashing.
  Image permissions, partial heap pages, worker stacks and mmap/sbrk remain on
  checked callbacks. Misaligned cross-page operations fall back to callbacks.
  The table costs 8 MiB on a 64-bit host; no 4 GiB reservation is used.
- Save guest register/VFP/exception/errno state and reap joins only at actual
  scheduling boundaries. Keep the 0.3.8 bounded timeslices and loading gate.
- Increase the PCM queue cap from 100 to 250 ms at 44.1 kHz stereo. Preserve
  alignment, partial-write return counts, blocking retry and stop/flush behavior.
  This adds potential audio latency and cannot compensate for sustained CPU
  overload. Empty-queue observations at writes are not exact audible underruns.
- Remove unconditional stderr heartbeat writes and reduce synchronous input
  checkpoints. Retain first-input, failure and five-second performance reports.
- Record separate input-callback, guest-frame and presentation times, maximum
  frame time, queued PCM duration, empty-write/backpressure counts and fast pages.

Joystick mapping, HD resources, 1280x720 resolution and Chinese remain unchanged.
No claimed Switch frame rate, continuous audio or complete gameplay verification.
Use sdmc:/switch/deadspacemobile/diagnostics.log after a hardware run to locate
remaining bottlenecks. Short queue and long guest frames implicate CPU work;
long presentation times implicate GPU/presentation. Input time includes guest
callbacks and any worker time scheduled inside those calls.
