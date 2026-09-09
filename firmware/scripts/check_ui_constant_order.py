#!/usr/bin/env python3
"""Verify the narrow UI constant visibility repair."""

from hashlib import sha256
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "src/app/AppRuntime.cpp"
THEME = ROOT / "src/ui/Theme.h"
RENDERER = ROOT / "src/ui/ScreenRenderer.inc"

EXPECTED = [
    ("UI_BG", "0,0,0"),
    ("UI_WHITE", "255,255,255"),
    ("UI_MUTED", "120,120,120"),
    ("UI_BORDER", "60,60,60"),
    ("UI_GREEN", "105,215,145"),
    ("UI_BLUE", "120,170,240"),
    ("UI_WARN", "220,180,80"),
]
EXPECTED_RENDERER_SHA256 = "ca1518da597417ce8dc2547ad7c8fbaff4d67b25833f0723d2d6da4fb89370e8"

errors = []
app = APP.read_text()
theme = THEME.read_text()

theme_include = '#include "../ui/Theme.h"'
renderer_include = '#include "../ui/ScreenRenderer.inc"'
if theme_include not in app or renderer_include not in app:
    errors.append("required UI includes are missing")
elif app.index(theme_include) > app.index(renderer_include):
    errors.append("Theme.h is not included before ScreenRenderer.inc")

position = -1
for name, rgb in EXPECTED:
    pattern = rf"static constexpr uint16_t {name}\s*=\s*RGB565\({re.escape(rgb)}\);"
    match = re.search(pattern, theme)
    if not match:
        errors.append(f"missing or changed definition: {name}=RGB565({rgb})")
    elif match.start() <= position:
        errors.append(f"definition order changed at {name}")
    else:
        position = match.start()

renderer_hash = sha256(RENDERER.read_bytes()).hexdigest()
if renderer_hash != EXPECTED_RENDERER_SHA256:
    errors.append(f"ScreenRenderer.inc changed: {renderer_hash}")

if errors:
    for error in errors:
        print(f"FAIL: {error}")
    sys.exit(1)

print("PASS: Theme.h precedes ScreenRenderer.inc")
print("PASS: 7/7 authoritative UI color definitions and order match")
print(f"PASS: ScreenRenderer.inc unchanged ({renderer_hash})")
print("Firmware build: REQUIRES AUTHORITATIVE LOCAL BUILD")
