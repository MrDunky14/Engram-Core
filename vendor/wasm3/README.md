# wasm3 vendor tree

Upstream WebAssembly interpreter: **wasm3** (MIT).

- **`upstream/`**: shallow clone used for MSVC object builds (`m3_core.c`, `m3_env.c`, …).
- Canonical project: https://github.com/wasm3/wasm3

Do **not** hand-edit upstream sources; regenerate with `git -C upstream pull` after bumping pinned revision if needed.

## Windows build snippet

See **`scripts\compile_phase14_gate.bat`** for the exact `cl` object list bundled into `phase14_gate.exe`.
