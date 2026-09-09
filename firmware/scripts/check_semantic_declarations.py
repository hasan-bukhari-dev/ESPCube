#!/usr/bin/env python3
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1] / "src"
buttons=(root/"input/Buttons.h").read_text(); touch=(root/"input/Touch.h").read_text(); home=(root/"app/HomeGesture.h").read_text(); profiles=(root/"app/ProfileManager.h").read_text()
checks={
"Button declarations":all(x in buttons for x in ("struct ButtonSnapshot","class Buttons","void begin();","ButtonSnapshot sample() const;")),
"Touch declarations":all(x in touch for x in ("struct TouchPoint","class Touch","void begin();","bool poll(TouchPoint &point);","onInterrupt();","readRegister(","readPoint(","transform(","static volatile bool pending_;")),
"HomeGesture declarations":all(x in home for x in ("class HomeGesture","bool update(bool aPressed, bool cPressed, uint32_t now);","uint32_t since = 0;","bool triggeredFlag = false;")),
"ProfileManager declaration":all(x in profiles for x in ("class ProfileManager","UIScreen screen = UIScreen::MOUSE;","Profile profile = Profile::MOUSE;")),
}
defs=[]
for p in (root/"input/Buttons.cpp",root/"input/Touch.cpp",root/"app/HomeGesture.cpp"): defs += re.findall(r"\b(?:void|bool|ButtonSnapshot|volatile bool)\s+(?:IRAM_ATTR\s+)?([A-Za-z_]\w*::[A-Za-z_]\w*)",p.read_text())
checks["no duplicate qualified definitions"]=len(defs)==len(set(defs))
for k,v in checks.items(): print(f"{'PASS' if v else 'FAIL'}: {k}")
bad=[k for k,v in checks.items() if not v]; print(f"RESULT: {len(checks)-len(bad)}/{len(checks)} PASS"); raise SystemExit(bool(bad))
