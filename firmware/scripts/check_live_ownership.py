#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]/"src"
all_text="\n".join(p.read_text(errors="ignore") for p in root.rglob("*") if p.is_file())
setup=(root/"app/SetupRuntime.inc").read_text(); buttons=(root/"input/ButtonsRuntime.inc").read_text(); profiles=(root/"app/ProfileManager.inc").read_text(); state=(root/"app/RuntimeState.inc").read_text(); assembly=(root/"app/AppRuntime.cpp").read_text()
checks={
"objects instantiated":all(x in state for x in ("Buttons buttons;","Touch touch;","HomeGesture homeGesture;","ProfileManager profiles;")),
"Buttons live setup":"buttons.begin();" in setup,
"Buttons live sampling":"buttons.sample();" in buttons,
"no direct button setup/sampling":not any(x in setup+buttons for x in ("pinMode(BTN_LEFT","pinMode(BTN_MIDDLE","pinMode(BTN_RIGHT","digitalRead(\n            BTN_")),
"Touch live setup":"touch.begin();" in setup,
"Touch live polling":"touch.poll(point)" in profiles,
"legacy Touch fragments detached":"TouchCore.inc" not in assembly and "TouchRuntime.inc" not in assembly,
"legacy Touch ownership absent":not any(x in all_text for x in ("touchPending","lastTouchActionMs","TOUCH_ACTION_COOLDOWN_MS","void initTouch()","bool touchReadRegister(","bool touchReadPoint(","void transformTouch(","onTouchInterrupt")),
"HOME live update":buttons.count("homeGesture.update(")==2,
"legacy HOME state absent":not any(x in all_text for x in ("homeComboSince","homeComboTriggered","HOME_COMBO_MS")),
"profile state live":"profiles.profile" in all_text and "profiles.screen" in all_text,
"legacy profile primitives absent":"currentProfile" not in all_text and "currentScreen" not in all_text,
}
for k,v in checks.items(): print(f"{'PASS' if v else 'FAIL'}: {k}")
bad=[k for k,v in checks.items() if not v];print(f"RESULT: {len(checks)-len(bad)}/{len(checks)} PASS");raise SystemExit(bool(bad))
