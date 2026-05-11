# Security policy — Engram Core (v1.0.0)

## What Engram Core is

Engram Core is a **sovereign, local, air-gapped-capable** agent: it runs on **your Windows machine**, ingests into a **persistent graph**, and may **read the screen**, **drive keyboard/mouse**, and use **local TTS**. It is **not** a hosted multi-tenant service.

## Primary safeguards

1. **ShadowBrain — microsecond-scale motor veto**  
   Before unsafe motor sequences are handed to the OS, the **ShadowBrain** path can **veto** plans that match disk/OS safety policy. **`/status`** reports **Worst ShadowBrain check** (microseconds). This is **defense in depth**, not a formal proof against all attacks.

2. **ESC key — kill switch**  
   Holding **ESC** triggers the **hardware-oriented kill switch** that **stops motor execution**. Use this if automation misbehaves.

## Explicitly unsupported / out of scope

- **Binding the daemon to open network sockets**, ad-hoc RPC, or exposing the console to the **public internet** is **unsupported** and **voids** the safety assumptions above. Engram Core is **not** designed as a network-facing server.
- **Not** a replacement for enterprise EDR, DLP, or formal verification.

## Reporting vulnerabilities

Report security issues **privately** (GitHub Security Advisories or maintainer contact) with reproduction steps. Do not post exploit details in public issues before coordinated disclosure.

## Release integrity (v1.0.0)

Binaries are **unsigned**. Verify **SHA-256** hashes published with each GitHub Release.

**Copyright 2026 Krishna Singh** (see [LICENSE](LICENSE)).
