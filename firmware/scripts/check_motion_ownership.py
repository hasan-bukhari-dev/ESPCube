#!/usr/bin/env python3
"""Source-level parity and live-integration checks for Motion extraction."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
ORACLE = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
SRC = ROOT / "src"
NEW_CPP = SRC / "input/Motion.cpp"
NEW_H = SRC / "input/Motion.h"


def body(text: str, signature: str) -> str:
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise ValueError(f"unterminated body: {signature}")


def tokens(text: str) -> list[str]:
    text = re.sub(r"//.*?$|/\*.*?\*/", "", text, flags=re.M | re.S)
    return re.findall(r"[A-Za-z_]\w*|\d+\.\d+f?|\d+|==|!=|<=|>=|&&|\|\||\+\+|--|\+=|-=|[{}()\[\];,.*+\-/%<>=?:!]", text)


checks: list[tuple[str, bool]] = []
cpp = NEW_CPP.read_text()
header = NEW_H.read_text()
app = (SRC / "app/AppRuntime.cpp").read_text()
runtime = (SRC / "app/RuntimeState.inc").read_text()
setup = (SRC / "app/SetupRuntime.inc").read_text()
loop = (SRC / "app/LoopRuntime.inc").read_text()

checks.append(("Motion class and owned QMI8658/state declared", all(x in header for x in (
    "class Motion", "QMI8658 imu", "float biasY", "float histY[3]",
    "float filteredX", "float remainderX", "uint32_t lastSampleUs",
    "bool stationary", "float mouseScrollAccumulator"))))
checks.append(("single authoritative Motion instance", "Motion motion;" in runtime))
checks.append(("live setup uses Motion::begin", "if (!motion.begin())" in setup))
checks.append(("live loop uses Motion::update in preserved slot",
               "updateButtons();\n    motion.update(buttons, profiles, hid);\n    updateTouch();" in loop))
checks.append(("legacy motion includes detached",
               "MotionRuntime.inc" not in app and "MotionCore.inc" not in app and
               not (SRC / "input/MotionRuntime.inc").exists() and
               not (SRC / "input/MotionCore.inc").exists()))

legacy_definitions = []
for path in SRC.rglob("*"):
    if not path.is_file() or path in (NEW_CPP, NEW_H):
        continue
    text = path.read_text(errors="ignore")
    for pattern in (r"\bQMI8658\s+imu\b", r"\bfloat\s+biasY\s*=", r"\bfloat\s+histY\[3\]\s*=",
                    r"\bfloat\s+filteredX\s*=", r"\bfloat\s+remainderX\s*=",
                    r"\bvoid\s+updateGyro\s*\("):
        if re.search(pattern, text):
            legacy_definitions.append(str(path.relative_to(SRC)))
checks.append(("duplicate legacy motion ownership removed", not legacy_definitions))

constants = {
    "SAMPLE_INTERVAL_US": "8000", "MAX_VALID_DPS": "470.0f",
    "REST_DELTA_Y": "1.35f", "REST_DELTA_Z": "1.35f", "REST_SOFT_DELTA": "2.60f",
    "REST_CAPTURE_DPS": "9.0f", "REST_EXIT_DPS": "5.0f", "REST_SCORE_MAX": "32",
    "REST_SCORE_ENTER": "22", "REST_SNAP_RATE": "0.55f", "REST_TRACK_RATE": "0.035f",
    "MICRO_DRIFT_MAX_DPS": "4.0f", "MICRO_DRIFT_DELTA": "1.50f", "MICRO_BIAS_RATE": "0.008f",
    "HORIZONTAL_SIGN": "+1.0f", "VERTICAL_SIGN": "-1.0f",
    "DEADZONE_HORIZONTAL": "2.2f", "DEADZONE_VERTICAL": "1.8f",
    "BASE_SENSITIVITY": "15.0f", "ALPHA_MIN": "0.18f", "ALPHA_MAX": "0.72f",
    "ALPHA_SPEED_SCALE": "0.010f", "MAX_ACCEL": "1.55f", "ACCEL_FULL_AT_DPS": "160.0f",
    "MOUSE_SCROLL_ARM_MS": "90", "MOUSE_SCROLL_ACTIVATE_DPS": "5.0f",
    "MOUSE_SCROLL_NOTCH": "1.60f", "MOUSE_SCROLL_CONTINUOUS_START": "20.0f",
    "MOUSE_SCROLL_CONTINUOUS_FULL": "80.0f", "MOUSE_SCROLL_CONTINUOUS_MIN_RATE": "30.0f",
    "MOUSE_SCROLL_CONTINUOUS_MAX_RATE": "65.0f",
}
checks.append(("all 30 motion constants unchanged", all(
    re.search(rf"\b{name}\s*=\s*{re.escape(value)}\s*;", cpp) for name, value in constants.items())))

init_markers = [
    "Wire.begin(Board::I2cSda, Board::I2cScl)", "Wire.setClock(400000)", "delay(100)",
    "imu.begin(Board::I2cSda, Board::I2cScl, QMI8658_ADDRESS_LOW)",
    "imu.begin(Board::I2cSda, Board::I2cScl, QMI8658_ADDRESS_HIGH)",
    "imu.setGyroRange(QMI8658_GYRO_RANGE_512DPS)", "imu.setGyroODR(QMI8658_GYRO_ODR_250HZ)",
    "imu.setGyroUnit_dps(true)", "imu.enableGyro(true)",
]
positions = [cpp.find(marker) for marker in init_markers]
checks.append(("QMI8658 initialization values/order unchanged",
               all(p >= 0 for p in positions) and positions == sorted(positions)))

if ORACLE:
    old_runtime = (ORACLE / "firmware/src/input/MotionRuntime.inc").read_text()
    old_core = (ORACLE / "firmware/src/input/MotionCore.inc").read_text()
    old_buttons = (ORACLE / "firmware/src/input/ButtonsRuntime.inc").read_text()
    checks.append(("update algorithm body token-identical",
                   tokens(body(old_runtime, "void updateGyro()")) ==
                   tokens(body(cpp, "void Motion::update("))))
    helper_pairs = [
        (old_core, "float median3(", cpp, "float Motion::median3("),
        (old_core, "void addHistory(", cpp, "void Motion::addHistory("),
        (old_core, "float softDeadzone(", cpp, "float Motion::softDeadzone("),
        (old_core, "float adaptiveFilter(", cpp, "float Motion::adaptiveFilter("),
        (old_core, "float accelerationFor(", cpp, "float Motion::accelerationFor("),
        (old_core, "bool sampleValid(", cpp, "bool Motion::sampleValid("),
        (old_core, "void clearPointerMotion(", cpp, "void Motion::clearPointerMotion("),
        (old_buttons, "void enterRest(", cpp, "void Motion::enterRest("),
        (old_buttons, "void exitRest(", cpp, "void Motion::exitRest("),
    ]
    checks.append(("nine helper bodies token-identical", all(
        tokens(body(a, sa)) == tokens(body(b, sb)) for a, sa, b, sb in helper_pairs)))

failed = [name for name, ok in checks if not ok]
for name, ok in checks:
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
if failed:
    if legacy_definitions:
        print("Legacy definitions:", ", ".join(sorted(set(legacy_definitions))))
    sys.exit(1)
print(f"RESULT: {len(checks)}/{len(checks)} PASS")
print("Firmware build: REQUIRES AUTHORITATIVE LOCAL BUILD")
