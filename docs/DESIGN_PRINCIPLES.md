# ESPCube Design Principles

> Product rules that future ESPCube changes should preserve.

[← Back to README](../README.md) · [Adding a Profile](ADDING_A_PROFILE.md) · [Architecture](ARCHITECTURE.md)

---

# 1. Universal first

ESPCube is a general handheld interface, not a single-game gadget.

A profile may target a specific use case, but the device itself should remain useful outside that profile.

---

# 2. Standard HID before custom host dependencies

If an interaction can be represented cleanly through standard Bluetooth HID, prefer that path.

Benefits:

- ordinary-computer compatibility
- no custom driver
- no Companion dependency for baseline controls
- simpler recovery
- clearer failure boundaries

---

# 3. The Companion extends the product

The Windows Companion exists for capabilities that genuinely need host-side software.

Examples:

- local Whisper inference
- native Windows text injection
- system loopback audio capture
- trusted Wi-Fi provisioning
- TCP Speaker transport

It should not become the reason basic controls work at all.

---

# 4. HOME is a physical invariant

**A + C returns HOME.**

That rule matters more than any individual profile mapping.

A profile should never trap the user.

---

# 5. Profiles own temporary resources, not the whole device

A profile may temporarily own:

- HID state
- audio state
- BLE service behavior
- network state
- UI state

On exit, it must release what it owns.

---

# 6. Wi-Fi only when bandwidth justifies it

Wi-Fi is not a default dependency.

Speaker uses Wi-Fi because continuous PCM is a high-bandwidth data path.

A low-volume control profile should not enable Wi-Fi merely because the hardware supports it.

---

# 7. Separate control plane from data plane when useful

Speaker demonstrates the pattern:

```text
BLE = readiness / control / provisioning
TCP = PCM data
```

This keeps control reliable and explicit while giving bulk data an appropriate transport.

---

# 8. Local processing where practical

v1 speech inference runs locally on the Windows PC.

The production path does not require cloud transcription.

This improves:

- independence from an external API
- predictable product behavior
- privacy boundary clarity
- offline capability for the inference path itself

---

# 9. Credentials do not belong in public firmware

Personal Wi-Fi credentials must not be hardcoded into the public source.

The Companion owns local trusted-network state.

The repository keeps credential constants blank.

---

# 10. Recover instead of thrashing

Short failures should not force expensive full resets when a scoped recovery exists.

Examples:

- BLE disconnect → Grace
- audio endpoint change → restart capture/DSP
- TCP write failure → reconnect Speaker path
- speech parity error → reject one utterance

---

# 11. Validate data before interpreting it

Speech transport is checked before Whisper inference.

The implementation tracks:

- sequence
- frame count
- sample count
- notification failures
- invalid packets

A plausible transcript is not proof that the transport was correct.

---

# 12. Stable behavior beats release-time elegance

A large architectural rewrite immediately before release is risky.

v1 intentionally preserves the proven coordinator architecture.

Refactors should happen behind explicit tests and invariants rather than because a different abstraction looks cleaner.

---

# 13. Public source is the source of truth

A release should not depend on an old private folder or untracked machine state.

The v1 release process validated:

```text
public source
→ fresh clone
→ build
→ flash
→ install
→ physical runtime
```

---

# 14. Release proof is physical

Firmware compile success is not enough.

Desktop compile success is not enough.

A shipping change should be proven on the actual Cube and through the installed Companion.

---

# 15. Documentation is part of the product

Every major feature should answer:

- what does the user do?
- what transport does it use?
- what are its dependencies?
- what does failure look like?
- how does it recover?
- how does it exit safely?

The root README stays user-first. Deeper docs carry the engineering detail.

---

# Practical review checklist

Before accepting a major change:

- [ ] Does Mouse still work without Companion?
- [ ] Does A + C always recover HOME?
- [ ] Is Wi-Fi still off unless justified?
- [ ] Are credentials still absent from source?
- [ ] Does profile exit release temporary state?
- [ ] Are failures contained to the affected service?
- [ ] Is transport integrity checked where needed?
- [ ] Does the public source build cleanly?
- [ ] Was the change tested on hardware?
- [ ] Were docs updated?

---

These principles are intentionally stricter than “does it compile?” because ESPCube is meant to behave like a product, not a collection of demos.
