# Adding a Profile

ESPCube v1 keeps the proven firmware architecture intact.

A future profile should preserve these product rules:
- Standard/Mouse mode must remain available
- A+C must remain a hard HOME escape
- use standard Bluetooth HID when possible
- do not make the whole product depend on one OS, app, or game
- Wi-Fi should be enabled only when a profile truly needs high-bandwidth transport
- profile teardown must release held HID state and temporary resources

For v1, new profile work should be integrated through the existing UI/profile
coordinator in `firmware/src/main.cpp` and reusable functionality should live in
a focused `firmware/lib/ESPCube...` library.

A future architectural refactor can move profiles into dedicated modules once the
interface is stable and covered by regression tests.
