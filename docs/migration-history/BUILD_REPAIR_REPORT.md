# UI Constant Ordering Build Repair

## Status

**Firmware build: REQUIRES AUTHORITATIVE LOCAL BUILD**

No physical-device validation is claimed.

## Failure repaired

The semantic ownership checkpoint no longer includes `TouchRuntime.inc`. In the
authoritative partitioned bridge, that fragment supplied the seven UI color
constants immediately before `ScreenRenderer.inc` was included. Removing the
fragment therefore made the renderer compile before `UI_BG`, `UI_WHITE`,
`UI_MUTED`, `UI_BORDER`, `UI_GREEN`, `UI_BLUE`, and `UI_WARN` were declared.

## Exact change

- Restored the bridge's seven `static constexpr uint16_t` definitions, with
  identical `RGB565(...)` values and declaration order, in `src/ui/Theme.h`.
- Included `../ui/Theme.h` in `src/app/AppRuntime.cpp` immediately before
  `../ui/ScreenRenderer.inc`.
- Did not move or edit renderer functions.
- Did not modify Motion, Speaker, profile actions, Buttons, Touch, HOME,
  ProfileManager, display coordinates, UI behavior, or color values.

## Source evidence

Run from `firmware/`:

```sh
python3 scripts/check_ui_constant_order.py
```

The check proves include order, all seven exact values and their order, and the
unchanged checkpoint renderer SHA-256:
`ca1518da597417ce8dc2547ad7c8fbaff4d67b25833f0723d2d6da4fb89370e8`.

## Authoritative local checkpoint

```sh
cd firmware
pio run
```

This must be run with the user's authoritative Espressif 32 55.3.311 / Arduino
3.3.11 environment. A successful local build is not asserted by this package.
