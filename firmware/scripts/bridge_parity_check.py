#!/usr/bin/env python3
from pathlib import Path
import hashlib, re, sys

repo = Path(__file__).resolve().parents[2]
oracle = Path(sys.argv[1]).resolve()
src = repo / "firmware/src"
old = (oracle / "firmware/src/main.cpp").read_text(errors="surrogateescape")
parts = [
 "app/RuntimeState.inc", "input/MotionCore.inc", "input/TouchCore.inc",
 "profiles/TextProfile.inc", "app/HomeGesture.inc", "app/ProfileManager.inc",
 "input/TouchRuntime.inc", "ui/ScreenRenderer.inc", "input/ButtonsRuntime.inc",
 "input/MotionRuntime.inc", "app/SetupRuntime.inc", "profiles/SpeakerStream.inc",
 "app/LoopRuntime.inc"
]
new = "".join((src / p).read_text(errors="surrogateescape") for p in parts)
new = new.replace("void AppRuntime::begin()", "void setup()", 1).replace("void AppRuntime::update()", "void loop()", 1)

def digest_tree(root):
    h = hashlib.sha256()
    for p in sorted(x for x in root.rglob("*") if x.is_file()):
        h.update(p.relative_to(root).as_posix().encode()); h.update(p.read_bytes())
    return h.hexdigest()

def functions(text):
    pat = re.compile(r"(?m)^(?:static\s+)?(?:void|bool|float|const char\s*\*)\s+(?:IRAM_ATTR\s+)?([A-Za-z_]\w*)\s*\(")
    out = []
    for m in pat.finditer(text):
        brace = text.find("{", m.end())
        semi = text.find(";", m.end(), brace + 1)
        if brace < 0 or semi >= 0: continue
        depth = 0; i = brace
        while i < len(text):
            if text[i] == "{": depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    out.append((m.group(1), text[m.start():i+1])); break
            i += 1
    return out

old_f, new_f = functions(old), functions(new)
checks = {}
checks["function name/order"] = [n for n,_ in old_f] == [n for n,_ in new_f]
checks["all function bodies byte-identical"] = old_f == new_f
def frozen_constexprs(text):
    values = re.findall(r"static\s+constexpr\s+.*?;", text, flags=re.S)
    moved = ("BTN_LEFT", "BTN_MIDDLE", "BTN_RIGHT", "I2C_SDA", "I2C_SCL", "TOUCH_RST", "TOUCH_INT", "TOUCH_ADDR", "LCD_CS", "LCD_CLK", "LCD_MOSI", "LCD_RST", "LCD_DC", "LCD_BL")
    return [v for v in values if not any(name in v for name in moved)]
checks["all non-board constexpr declarations byte-identical"] = frozen_constexprs(old) == frozen_constexprs(new)
checks["firmware libraries byte-identical"] = digest_tree(oracle/"firmware/lib") == digest_tree(repo/"firmware/lib")
checks["companion byte-identical"] = digest_tree(oracle/"companion") == digest_tree(repo/"companion")

board = (src/"hardware/BoardConfig.h").read_text(); display = (src/"hardware/Display.cpp").read_text()
for name, value in {"ButtonA":"0", "ButtonB":"5", "ButtonC":"4", "I2cSda":"42", "I2cScl":"41", "TouchReset":"47", "TouchInterrupt":"48", "TouchAddress":"0x15", "LcdCs":"21", "LcdClock":"38", "LcdMosi":"39", "LcdReset":"40", "LcdDc":"45", "LcdBacklight":"46"}.items():
    checks[f"board {name}={value}"] = re.search(rf"\b{name}\s*=\s*{re.escape(value)}\b", board) is not None
checks["display constructor arguments"] = all(x in display for x in ("Board::LcdDc, Board::LcdCs, Board::LcdClock, Board::LcdMosi, GFX_NOT_DEFINED", "lcdBus, Board::LcdReset, 3, true, Board::DisplayWidth, Board::DisplayHeight, 0, 0, 0, 80"))

contracts = ["TCP_PORT = 47821", "TCP_PCM_FRAMES = 320", "128 * 1024", "12 * 320 * sizeof(int16_t)", "D2A_UNDERRUN_GRACE_MS", "SAMPLE_INTERVAL_US = 8000", "DEBOUNCE_MS = 18", "TOUCH_ACTION_COOLDOWN_MS = 220", "SPEECH_HOLD_MS", "MOUSE_SCROLL_ARM_MS", "HOME_COMBO_MS", "800;", "Wire.setClock(400000)", "QMI8658_GYRO_RANGE_512DPS", "QMI8658_GYRO_ODR_250HZ"]
for c in contracts: checks[f"frozen contract {c}"] = c in old and c in new

checks["HOME implementation unchanged"] = dict(old_f).get("goHome") == dict(new_f).get("goHome")
checks["touch transform unchanged"] = dict(old_f).get("transformTouch") == dict(new_f).get("transformTouch")
checks["motion update unchanged"] = dict(old_f).get("updateGyro") == dict(new_f).get("updateGyro")
checks["button/profile dispatch unchanged"] = dict(old_f).get("updateButtons") == dict(new_f).get("updateButtons")
checks["setup ordering unchanged"] = dict(old_f).get("setup") == dict(new_f).get("setup")
checks["loop ordering unchanged"] = dict(old_f).get("loop") == dict(new_f).get("loop")

for k,v in checks.items(): print(f"{'PASS' if v else 'FAIL'}: {k}")
bad = [k for k,v in checks.items() if not v]
print(f"RESULT: {len(checks)-len(bad)}/{len(checks)} PASS")
raise SystemExit(bool(bad))
