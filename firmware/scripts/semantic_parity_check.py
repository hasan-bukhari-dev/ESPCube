#!/usr/bin/env python3
from pathlib import Path
import hashlib, re, sys
repo=Path(__file__).resolve().parents[2]; oracle=Path(sys.argv[1]).resolve(); src=repo/'firmware/src'; old=(oracle/'firmware/src/main.cpp').read_text(errors='ignore')
checks={}
def has(path,*terms):
    s=(src/path).read_text(errors='ignore'); return all(t in s for t in terms)
checks['buttons pins and debounce']=has('input/Buttons.h','DebounceMs = 18') and has('hardware/BoardConfig.h','ButtonA = 0','ButtonB = 5','ButtonC = 4')
checks['touch repeated start']=has('input/Touch.cpp','Wire.endTransmission(false)','Wire.requestFrom((uint8_t)Board::TouchAddress, len, true)')
checks['touch transform and clamp']=has('input/Touch.cpp','screenX = 239 - (int16_t)rawY','screenY = (int16_t)rawX','constrain(screenX, 0, 239)','constrain(screenY, 0, 239)')
checks['touch reset timing']=has('input/Touch.cpp','delay(20)','delay(5)','delay(60)')
checks['HOME timing and latch']=has('app/HomeGesture.h','HoldMs = 800') and has('app/HomeGesture.cpp','now - since >= HoldMs')
checks['profile state owned']=has('app/ProfileManager.h','UIScreen screen','Profile profile')
checks['runtime call order']=has('app/LoopRuntime.inc','speechLink.maintain();','speakerControl.maintain();','serviceSpeakerControl();','serviceSpeakerTcpD2A();','updateButtons();','updateGyro();','updateTouch();','updateDisplay();','delay(1);')
for term in ['TCP_PORT = 47821','TCP_PCM_FRAMES = 320','128 * 1024','12 * 320 * sizeof(int16_t)','SAMPLE_INTERVAL_US = 8000','QMI8658_GYRO_RANGE_512DPS','QMI8658_GYRO_ODR_250HZ']:
    checks['frozen '+term]=term in old and any(term in p.read_text(errors='ignore') for p in src.rglob('*') if p.is_file())
def treehash(p):
 h=hashlib.sha256()
 for f in sorted(x for x in p.rglob('*') if x.is_file()): h.update(f.relative_to(p).as_posix().encode());h.update(f.read_bytes())
 return h.hexdigest()
checks['firmware libraries unchanged']=treehash(repo/'firmware/lib')==treehash(oracle/'firmware/lib')
checks['companion unchanged']=treehash(repo/'companion')==treehash(oracle/'companion')
for k,v in checks.items(): print(('PASS' if v else 'FAIL')+': '+k)
bad=[k for k,v in checks.items() if not v];print(f'RESULT: {len(checks)-len(bad)}/{len(checks)} PASS');raise SystemExit(bool(bad))
