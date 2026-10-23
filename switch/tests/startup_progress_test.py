#!/usr/bin/env python3
"""Optional actual-donor regression; no resources are uploaded to CI.

Measures loading progress without injecting any input. A manual framebuffer
review is still required to identify the page and does not replace Switch QA.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('probe',type=Path)
p.add_argument('data',type=Path)
p.add_argument('--hd',type=Path)
a=p.parse_args()
with tempfile.TemporaryDirectory() as t:
    w=Path(t)
    env={**os.environ,'SDL_AUDIODRIVER':'dummy','LIBGL_ALWAYS_SOFTWARE':'1','EGL_PLATFORM':'surfaceless'}
    env.pop('DS_SPLASH_TRACE',None)
    command=[str(a.probe.resolve()),str(a.data.resolve()/'lib/armeabi/libEAMGameDeadSpace.so'),str(a.data.resolve()),'1800',str(a.hd.resolve()) if a.hd else '',str(w/'frame.ppm'),'',str(w/'saves'),'16666667']
    run=subprocess.run(command,env=env,text=True,capture_output=True,timeout=300)
    if run.returncode:
        raise SystemExit('Host startup failed:\n'+run.stdout[-6000:]+run.stderr[-6000:])
    uploads=re.search(r'(\d+) texture uploads',run.stdout)
    switches=re.search(r'context switches=(\d+)',run.stdout)
    assert uploads and int(uploads[1])>=500,'Background loading did not reach the validated post-splash resource set by frame 1800'
    assert switches and int(switches[1])<100000,'Excessive context switching fragmented the loader timeslice'
    with (w/'frame.ppm').open('rb') as f:
        assert f.readline()==b'P6\n' and f.readline()==b'1280 720\n' and f.readline()==b'255\n'
        assert len(f.read())==1280*720*3
    print('PASS: no-input host startup; post-splash resource set loaded; bounded context switching. Page identity and Switch behavior require visual/hardware checks.')
