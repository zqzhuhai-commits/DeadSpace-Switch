# 0.3.8: let startup loading finish before the automatic splash transition

0.3.7 Switch logs show input callbacks returning, an advancing clock, and an
unchanging splash resource set. The headphone screen does not require a key.
The actual supported donor was reproduced with no input on a Linux host.
A temporary, fingerprint-checked probe of Splash::update showed its display
interval expiring while its loading-complete flag stayed false. Extending the
old run eventually set the flag and progressed automatically, so this was
severely fragmented loading, not proof of a permanently lost notification.

The runtime previously reset its instruction quantum on every JIT entry and
rotated contexts at every libc/JNI/GL dispatch. A display frame therefore
advanced the resource worker by many tiny fragments before returning to the
host's presentation/vsync path. Normal imports are now resumed inside the
remaining timeslice. Threads rotate on explicit yield, blocking, or exhaustion
of a bounded one-million-instruction slice, renewed on context changes.
Nested guest calls continue to retain their suspended context. The splash
condition, game data/code and loading-complete flag are not bypassed or forced.
No temporary ARM patch or Splash::update probe is included in this revision.

## Evidence and limits

With the supplied supported donor, repaired material, matching HD resources,
a fresh save root, 1280x720 desktop compatibility GL and an injected 16.67 ms
presentation clock, the old build still showed the headphone page at frame
1800; its loading flag finally advanced around frame 15900. The candidate
advanced without input to the Chinese difficulty-selection page by frame
1800; the temporary probe already observed loading complete at frame 480.
This is a host comparison, not a Switch wall-clock timing guarantee, and the
next page on a fresh save is not proof that the main menu or gameplay works.

The optional actual-donor regression can be run locally:

```
python3 switch/tests/startup_progress_test.py /path/to/ds-media-probe /path/to/data --hd /path/to/texture_hd
```

It checks that the validated post-splash resource set is loaded by frame 1800
without injected input and that switching does not explode. It intentionally
does not claim page identity from counters alone; framebuffer review supplied
that evidence. Host ctest and the standard Switch CI run separately.

Remaining hardware checks: automatic earphone-page exit, waiting time, audio
continuity, menu input, saving and gameplay. Chinese and controller changes
from 0.3.7 are retained.
