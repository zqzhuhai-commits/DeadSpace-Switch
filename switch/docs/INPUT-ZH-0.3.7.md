# 0.3.7 input and Chinese resource candidate

Based on the released 0.3.6 revision c038ffa6. This is a repair candidate,
not a claim of verified Switch gameplay or a completed Chinese NSP.

- Start/+ reaches Android key 108 instead of stopping the frontend.
- Switch panel input uses libnx HID directly; SDL duplicate touch/mouse events
  are ignored on Switch. Desktop finger and mouse input remain available.
- Menus start with a visible cursor: D-pad moves it; SDL B (the existing
  Nintendo-style accept mapping) presses/releases a pointer at that location.
  L3/R3 toggle cursor mode; Start restores it; stick movement dismisses it.
- Left stick sustains a front touchscreen movement drag. Right stick restarts
  rear touchpad gestures every frame, following android/input_bridge.cpp.
  Pointer module 1100 must survive the guest ABI; it must not become 1000.
- Existing face/shoulder/back Android key mappings remain. ZL/ZR additionally
  send keys 102/103. Final button labels/action semantics need hardware checks.
- Locale is zh_CN, with language zh. The game's Platform::getLocale normalizes
  underscore to hyphen before Settings::getDeviceLanguage matches a language.
  Existing user saves may retain their explicitly selected language.

## Single NSP packaging

Use the supported original donor directory for --data. Reanimated's embedded
libDeadSpace32.so has a different SHA1 and is deliberately not imported as code.
Pass the user's APK as both --hd-apk and --chinese-apk to package_nsp.py, together
with the existing local build/package arguments. Chinese strings and the
Ascender Unicode font are included in the same RomFS as the supported runtime
and matched HD textures. No separate SD resource copy is required.

The Chinese overlay compares the string database schema against the supported
donor's English database and fails before writing if incompatible. This check
is necessary but does not prove every string ID, font selection or translation
renders correctly. It does not force an English fallback to display Chinese.

## Validation and remaining work

Host input regression covers Start, pointer press/release, mouse deduplication,
left stick neutral/release, continuous rear touchpad pulses, triggers and cursor
recovery. Resource tests cover allowlisted files, schema rejection and preserving
supported code. Switch cross-compilation runs in the existing PR workflow.

On hardware: clear/rotate diagnostics.log, verify the earphone screen advances,
check panel and docked cursor, then movement, aiming, shooting/reload, pause,
Chinese menu/dialogue and saving. A changing frame counter alone is not proof
that the earphone screen is fixed. First-load startup still takes time; this
patch does not claim to remove the reported 15-second resource/JIT delay.
