# Ownership Integration Report

Firmware build status: **REQUIRES AUTHORITATIVE LOCAL BUILD**. Physical validation was not performed.

## Live integration completed

| Owner | Runtime integration | Removed duplicate ownership |
|---|---|---|
| `Buttons buttons` | Instantiated in `RuntimeState.inc`; `buttons.begin()` replaces setup GPIO/read initialization; `buttons.sample()` replaces the three direct loop reads; debounce states/timestamps now use the object | `BTN_LEFT/MIDDLE/RIGHT` aliases, global stable states/timestamps, runtime `DEBOUNCE_MS`, direct setup/sample calls |
| `Touch touch` | Instantiated; `touch.begin()` executes at the original setup position; synchronous `touch.poll(point)` executes at the original loop position before `handleTouchAction` | legacy ISR flag, cooldown global, register/point/transform/init/update functions and both Touch `.inc` assembly entries |
| `HomeGesture homeGesture` | Instantiated; `homeGesture.update()` is called in both Speaker-priority and general HOME branches | primitive HOME timestamp/latch and duplicate 800 ms constant |
| `ProfileManager profiles` | Instantiated with Mouse defaults; all active-profile/screen reads and writes route through `profiles.profile`/`profiles.screen` | `currentProfile`, `currentScreen`, and their duplicate enum/state block |

Button debounce/action statement order, Touch repeated-start/reset/cooldown/transform behavior, HOME action ordering, Speaker early-return priority, profile mappings, and top-level loop order remain unchanged except for the explicit ownership method boundaries.

## Checks

- `check_semantic_declarations.py`: declaration/definition and duplicate-qualified-definition audit.
- `check_live_ownership.py`: proves object instantiation/live calls and rejects legacy duplicate ownership.
- Host syntax-only checks cover `Buttons.cpp`, `Touch.cpp`, `HomeGesture.cpp`, and standalone `ProfileManager.h` using external minimal Arduino declarations.

## Deliberately untouched

Motion, rendering, Speaker transport, and broader profile lifecycle were not refactored in this pass. Their existing bridge fragments remain.
